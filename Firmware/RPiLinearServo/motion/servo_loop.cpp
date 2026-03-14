// Servo control loop — explicit state machine.
//
// States:
//   IDLE       Motor disabled, waiting for input.
//   HOMING     Blocking homing sequence in progress.
//   MOVING     Stepgen is running toward a target.
//   HOLDING    At target, motor enabled, idle timer counting.
//
// Transitions are evaluated once per poll in a single switch, so there
// are no conflicting updates from separate functions.

#include "servo_loop.h"
#include "hall_verify.h"
#include "dormant.h"
#include "config.h"
#include "pins.h"
#include "stepgen.h"
#include "homing.h"
#include "status_led.h"
#include "pwm_input.h"
#include "position_store.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <cstdio>

// ── state machine ──────────────────────────────────────────────────

enum class ServoState { IDLE, HOMING, MOVING, HOLDING };

static ServoState      s_state;
static absolute_time_t s_hold_since;          // when HOLDING was entered
static absolute_time_t s_boot_time;
static absolute_time_t s_next_hall_sample;
static bool            s_pwm_ever_seen;       // latched: first-ever valid PWM
static bool            s_pwm_had_signal;      // set by valid pulse, cleared on timeout action

// ── helpers ────────────────────────────────────────────────────────

static void enter_state(ServoState next) {
    if (next == s_state) return;
    s_state = next;
    switch (next) {
    case ServoState::IDLE:
        gpio_put(PIN_EN, EN_DISABLE);
        status_led_set(LedStatus::IDLE);
        printf("[servo] → IDLE\n");
        break;
    case ServoState::HOMING:
        status_led_set(LedStatus::HOMING);
        printf("[servo] → HOMING\n");
        break;
    case ServoState::MOVING:
        gpio_put(PIN_EN, EN_ENABLE);
        status_led_set(LedStatus::MOVING);
        break;
    case ServoState::HOLDING:
        s_hold_since = get_absolute_time();
        status_led_set(LedStatus::HOLDING);
        printf("[servo] → HOLDING\n");
        break;
    }
}

// Try to start a move toward the current PWM target.
// Returns true if a move was issued.
static bool try_start_pwm_move() {
    if (!g_homed || stepgen_is_busy() || g_stall_fault) return false;
    if (!pwm_input_is_valid()) return false;

    float target_mm  = g_config.pwm_to_mm(pwm_input_get_us());
    float current_mm = (float)stepgen_get_position()
                       / g_config.steps_per_mm();
    float error_mm   = target_mm - current_mm;

    if (error_mm > g_config.deadband_mm
        || error_mm < -g_config.deadband_mm) {
        int32_t delta = (int32_t)(error_mm * g_config.steps_per_mm());
        enter_state(ServoState::MOVING);
        stepgen_move_accel(delta,
                           g_config.default_speed_hz(),
                           g_config.accel_hz_per_s());
        return true;
    }
    return false;
}

// ── public API ─────────────────────────────────────────────────────

void servo_loop_init() {
    s_state            = ServoState::IDLE;
    s_hold_since       = get_absolute_time();
    s_boot_time        = get_absolute_time();
    s_next_hall_sample = get_absolute_time();
    s_pwm_ever_seen    = false;
    s_pwm_had_signal   = false;
}

void servo_loop_move(int32_t steps, uint32_t speed_hz, uint32_t accel_hz_per_s) {
    enter_state(ServoState::MOVING);
    stepgen_move_accel(steps, speed_hz, accel_hz_per_s);
}

void servo_loop_run(uint32_t speed_hz) {
    enter_state(ServoState::MOVING);
    stepgen_run(speed_hz);
}

void servo_loop_stop() {
    stepgen_stop();
    enter_state(ServoState::HOLDING);
}

void servo_loop_enable() {
    if (s_state == ServoState::IDLE) {
        // Force transition even from IDLE — enable driver, skip to HOLDING
        s_state = ServoState::IDLE; // ensure enter_state sees a change
        gpio_put(PIN_EN, EN_ENABLE);
        s_hold_since = get_absolute_time();
        s_state = ServoState::HOLDING;
        status_led_set(LedStatus::HOLDING);
        printf("[servo] → HOLDING\n");
    }
}

void servo_loop_disable() {
    stepgen_stop();
    // Force to IDLE regardless of current state
    s_state = ServoState::HOLDING; // ensure enter_state sees a change
    enter_state(ServoState::IDLE);
}

void servo_loop_home() {
    if (stepgen_is_busy()) return;
    enter_state(ServoState::HOMING);
    homing_run();                       // blocking
    enter_state(ServoState::HOLDING);
}

void servo_loop_poll() {
    // ── 1. sample inputs ───────────────────────────────────────────
    pwm_input_poll();
    const bool pwm_valid = pwm_input_is_valid();
    const bool pwm_tout  = pwm_input_is_timed_out();
    const bool busy      = stepgen_is_busy();

    if (pwm_valid) {
        if (!s_pwm_ever_seen) {
            printf("[pwm] signal acquired (%lu µs)\n",
                   (unsigned long)pwm_input_get_us());
        }
        s_pwm_ever_seen  = true;
        s_pwm_had_signal = true;
    }

    // ── 2. state machine ──────────────────────────────────────────
    switch (s_state) {

    // ·············· IDLE ··············
    case ServoState::IDLE:
        // First valid PWM while not yet homed → auto-home then hold
        if (pwm_valid && !g_homed && !busy) {
            printf("[pwm] not homed — starting auto-home\n");
            enter_state(ServoState::HOMING);
            homing_run();                       // blocking
            enter_state(ServoState::HOLDING);
            break;
        }
        // Valid PWM with actionable position error → move
        if (pwm_valid) {
            if (try_start_pwm_move()) break;
        }
        // Dormant sleep: no PWM activity at all
        {
            bool no_pwm_ever = !pwm_input_ever_valid()
                && absolute_time_diff_us(s_boot_time,
                                         get_absolute_time()) > 5000000;
            if ((pwm_tout && !s_pwm_had_signal) || no_pwm_ever) {
                if (dormant_try_enter()) {
                    // Full re-init happened; servo_loop_init() was called.
                    return;
                }
            }
        }
        break;

    // ·············· MOVING ··············
    case ServoState::MOVING:
        // Hall logging during motion
        if (absolute_time_diff_us(s_next_hall_sample,
                                  get_absolute_time()) >= 0) {
            hall_log_sample();
            s_next_hall_sample = make_timeout_time_ms(4);
        }
        // Move finished?
        if (!busy) {
            hall_verify_post_move();
            // If PWM already disappeared during the move, go straight
            // to IDLE — no point entering HOLDING with a dead signal.
            if (pwm_tout && s_pwm_had_signal
                && g_config.pwm_zero_disables) {
                printf("[pwm] signal lost during move — disabling\n");
                s_pwm_had_signal = false;
                enter_state(ServoState::IDLE);
            } else {
                enter_state(ServoState::HOLDING);
            }
        }
        break;

    // ·············· HOLDING ··············
    case ServoState::HOLDING: {
        // PWM still active → track position, start new move if needed
        if (pwm_valid) {
            if (try_start_pwm_move()) break;
        }

        // PWM signal lost → disable immediately
        if (pwm_tout && s_pwm_had_signal && g_config.pwm_zero_disables) {
            printf("[pwm] signal lost — disabling motor\n");
            stepgen_stop();
            s_pwm_had_signal = false;
            enter_state(ServoState::IDLE);
            break;
        }

        // Auto-disable after idle timeout
        if (g_config.auto_disable_ms > 0) {
            uint32_t held_ms = (uint32_t)(absolute_time_diff_us(
                s_hold_since, get_absolute_time()) / 1000);
            if (held_ms >= g_config.auto_disable_ms) {
                printf("[auto] motor disabled after %lu ms idle\n",
                       (unsigned long)g_config.auto_disable_ms);
                enter_state(ServoState::IDLE);
                break;
            }
        }
        break;
    }

    // ·············· HOMING ··············
    case ServoState::HOMING:
        // homing_run() is blocking — we should never poll in this state.
        enter_state(ServoState::IDLE);
        break;
    }
}

// Per-iteration servo control: PWM tracking, auto-home, move-complete
// transitions, post-move hall verification, auto-disable, hall logging.

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

static bool            s_was_busy            = false;
static absolute_time_t s_idle_since;
static bool            s_auto_disabled       = false;
static LedStatus       s_prev_led            = LedStatus::OFF;
static bool            s_pwm_was_valid       = false;
static bool            s_pwm_homing_triggered = false;
static absolute_time_t s_next_hall_sample;
static absolute_time_t s_boot_time;

void servo_loop_init() {
    s_idle_since       = get_absolute_time();
    s_next_hall_sample = get_absolute_time();
    s_boot_time        = get_absolute_time();
}


static void poll_pwm_input() {
    pwm_input_poll();
    bool pwm_valid = pwm_input_is_valid();
    bool pwm_tout  = pwm_input_is_timed_out();

    // Rising edge: first valid pulse (or recovery from timeout)
    if (pwm_valid && !s_pwm_was_valid) {
        printf("[pwm] signal acquired (%lu µs)\n",
               (unsigned long)pwm_input_get_us());
    }

    // Auto-home on first valid PWM
    if (pwm_valid && !g_homed && !s_pwm_homing_triggered
        && !stepgen_is_busy()) {
        printf("[pwm] not homed — starting auto-home\n");
        s_pwm_homing_triggered = true;
        homing_run();
    }

    // Position tracking: move toward PWM target when homed & idle
    if (pwm_valid && g_homed && !stepgen_is_busy() && !g_stall_fault) {
        float target_mm  = g_config.pwm_to_mm(pwm_input_get_us());
        float current_mm = (float)stepgen_get_position()
                           / g_config.steps_per_mm();
        float error_mm   = target_mm - current_mm;

        if (error_mm > g_config.deadband_mm
            || error_mm < -g_config.deadband_mm) {
            int32_t delta_steps = (int32_t)(error_mm
                                           * g_config.steps_per_mm());
            gpio_put(PIN_EN, EN_ENABLE);
            status_led_set(LedStatus::MOVING);
            s_auto_disabled = false;
            stepgen_move_accel(delta_steps,
                               g_config.default_speed_hz(),
                               g_config.accel_hz_per_s());
        }
    }

    // PWM timeout: disable motor if configured
    if (pwm_tout && s_pwm_was_valid && g_config.pwm_zero_disables) {
        printf("[pwm] signal lost — disabling motor\n");
        stepgen_stop();
        gpio_put(PIN_EN, EN_DISABLE);
        status_led_set(LedStatus::IDLE);
        s_auto_disabled = true;
    }

    // Attempt dormant sleep when there is no PWM activity:
    //  - after signal-lost timeout (pwm_tout && s_pwm_was_valid handled above)
    //  - or if no valid pulse was ever received (pin held low from boot)
    //    with a 5 s grace period so USB has time to enumerate first
    bool no_pwm_ever = !pwm_input_ever_valid()
                       && absolute_time_diff_us(s_boot_time, get_absolute_time())
                          > 5000000;
    if ((pwm_tout && s_auto_disabled) || no_pwm_ever) {
        if (dormant_try_enter()) {
            s_pwm_was_valid        = false;
            s_pwm_homing_triggered = false;
            return;
        }
    }
    s_pwm_was_valid = pwm_valid;
}


static void poll_move_complete(bool busy) {
    if (s_was_busy && !busy) {
        if (status_led_get() == LedStatus::MOVING)
            status_led_set(LedStatus::HOLDING);

        hall_verify_post_move();
    }
    s_was_busy = busy;
}


static void poll_auto_disable(bool busy) {
    LedStatus cur_led = status_led_get();
    if (cur_led == LedStatus::HOLDING && s_prev_led != LedStatus::HOLDING) {
        s_idle_since    = get_absolute_time();
        s_auto_disabled = false;
    }
    s_prev_led = cur_led;

    if (g_config.auto_disable_ms > 0
        && !s_auto_disabled
        && !busy
        && cur_led == LedStatus::HOLDING) {
        uint32_t idle_ms = (uint32_t)(absolute_time_diff_us(
            s_idle_since, get_absolute_time()) / 1000);
        if (idle_ms >= g_config.auto_disable_ms) {
            gpio_put(PIN_EN, EN_DISABLE);
            status_led_set(LedStatus::IDLE);
            s_auto_disabled = true;
            printf("[auto] motor disabled after %lu ms idle\n",
                   (unsigned long)g_config.auto_disable_ms);
        }
    }
}


static void poll_hall_logging(bool busy) {
    if (busy
        && absolute_time_diff_us(s_next_hall_sample,
                                 get_absolute_time()) >= 0) {
        hall_log_sample();
        s_next_hall_sample = make_timeout_time_ms(4);  // 250 Hz
    }
}


void servo_loop_poll() {
    poll_pwm_input();

    bool busy = stepgen_is_busy();
    poll_move_complete(busy);
    poll_auto_disable(busy);
    poll_hall_logging(busy);
}

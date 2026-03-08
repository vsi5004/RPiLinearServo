// ── stepgen.cpp ─────────────────────────────────────────────────────────
// PIO-based STEP pulse generator with IRQ-counted position tracking.

#include "stepgen.h"
#include "config.h"
#include "stepgen.pio.h"       // auto-generated from stepgen.pio

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>

// ── PIO instance and state machine ─────────────────────────────────────
static PIO   s_pio       = pio0;
static uint  s_sm        = 0;
static uint  s_offset    = 0;
static uint  s_step_pin  = 0;
static uint  s_dir_pin   = 0;

// ── Volatile state shared with IRQ handler ─────────────────────────────
static volatile int32_t  s_position       = 0;   // exact step counter
static volatile int32_t  s_remaining      = 0;   // steps left (-1 = infinite)
static volatile bool     s_dir_forward    = true;
static volatile bool     s_running        = false;
static volatile bool     s_move_complete  = false;

// ── Non-volatile state ─────────────────────────────────────────────────
static uint32_t s_current_speed_hz = 0;
static bool     s_initialised      = false;

// ── Trapezoidal ramp profiler ──────────────────────────────────────────
// A 1 kHz repeating timer adjusts the PIO clock divider to implement
// acceleration / cruise / deceleration.  The PIO IRQ still handles
// exact step counting and auto-stop.
enum class RampPhase { IDLE, ACCEL, CRUISE, DECEL };

static volatile RampPhase s_ramp_phase = RampPhase::IDLE;
static float    s_ramp_speed  = 0.0f;     // current speed in Hz
static float    s_ramp_max    = 0.0f;     // target cruise speed in Hz
static float    s_ramp_accel  = 0.0f;     // acceleration in Hz/s
static const float RAMP_MIN_HZ = 200.0f;  // starting / min speed (~0.34 mm/s)
static repeating_timer_t s_ramp_timer;

static bool ramp_callback(repeating_timer_t *rt) {
    (void)rt;
    if (s_ramp_phase == RampPhase::IDLE) return true;
    if (!s_running) {
        s_ramp_phase = RampPhase::IDLE;
        return true;
    }

    const float dt = 0.001f;   // 1 ms tick
    int32_t remaining = s_remaining;

    // Steps needed to decelerate from current speed to ~zero: v²/(2a)
    float decel_steps = (s_ramp_speed * s_ramp_speed) / (2.0f * s_ramp_accel);

    switch (s_ramp_phase) {
    case RampPhase::ACCEL:
        s_ramp_speed += s_ramp_accel * dt;
        if (s_ramp_speed >= s_ramp_max) {
            s_ramp_speed = s_ramp_max;
            s_ramp_phase = RampPhase::CRUISE;
        }
        // Recalculate after speed change
        decel_steps = (s_ramp_speed * s_ramp_speed) / (2.0f * s_ramp_accel);
        if (remaining > 0 && (float)remaining <= decel_steps + 1.0f) {
            s_ramp_phase = RampPhase::DECEL;
        }
        break;

    case RampPhase::CRUISE:
        if (remaining > 0 && (float)remaining <= decel_steps + 1.0f) {
            s_ramp_phase = RampPhase::DECEL;
        }
        break;

    case RampPhase::DECEL:
        s_ramp_speed -= s_ramp_accel * dt;
        if (s_ramp_speed < RAMP_MIN_HZ) s_ramp_speed = RAMP_MIN_HZ;
        break;

    default:
        break;
    }

    // Apply new speed to PIO
    stepgen_set_frequency(s_pio, s_sm, (uint32_t)s_ramp_speed);
    s_current_speed_hz = (uint32_t)s_ramp_speed;
    return true;
}

// ── IRQ handler ────────────────────────────────────────────────────────
// Called on every PIO-generated STEP rising edge (PIO IRQ0).
// Body is ~10 instructions — safe at ≤ 10 kHz (100 µs budget).
static void pio_irq_handler() {
    // Clear the IRQ flag so we can receive the next one
    pio_interrupt_clear(s_pio, 0);

    // Update position
    if (s_dir_forward) {
        s_position++;
    } else {
        s_position--;
    }

    // Finite-move tracking
    if (s_remaining > 0) {
        s_remaining--;
        if (s_remaining == 0) {
            // Exact step count reached — stop the SM immediately
            pio_sm_set_enabled(s_pio, s_sm, false);
            // Force STEP low
            pio_sm_exec(s_pio, s_sm, pio_encode_set(pio_pins, 0));
            s_running = false;
            s_move_complete = true;
        }
    }
    // If s_remaining == -1 (continuous mode), we never stop here.
}

// ── Public API ─────────────────────────────────────────────────────────

void stepgen_init(uint step_pin, uint dir_pin) {
    s_step_pin = step_pin;
    s_dir_pin  = dir_pin;

    // DIR pin: regular GPIO output
    gpio_init(s_dir_pin);
    gpio_set_dir(s_dir_pin, GPIO_OUT);
    gpio_put(s_dir_pin, 0);

    // Load PIO program
    s_offset = pio_add_program(s_pio, &stepgen_program);
    stepgen_program_init(s_pio, s_sm, s_offset, s_step_pin);

    // Enable PIO IRQ0 → NVIC
    // PIO0_IRQ_0 is triggered by `irq nowait 0` in the PIO program.
    uint pio_irq_num = (s_pio == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0;

    // Enable IRQ source 0 on this PIO for our SM
    pio_set_irq0_source_enabled(s_pio, pis_interrupt0, true);

    irq_set_exclusive_handler(pio_irq_num, pio_irq_handler);
    irq_set_enabled(pio_irq_num, true);

    s_position      = 0;
    s_remaining     = 0;
    s_dir_forward   = true;
    s_running       = false;
    s_move_complete = false;
    s_current_speed_hz = 0;
    s_initialised   = true;

    // Start the 1 kHz ramp-profiler timer (runs in alarm IRQ context).
    // Callback is a no-op when ramp phase is IDLE.
    add_repeating_timer_ms(-1, ramp_callback, nullptr, &s_ramp_timer);

    printf("[stepgen] init OK: STEP=GP%u DIR=GP%u PIO%d SM%d\n",
           step_pin, dir_pin, pio_get_index(s_pio), s_sm);
}

void stepgen_set_dir(bool forward) {
    bool gpio_val = forward;
    if (g_config.dir_invert) gpio_val = !gpio_val;
    gpio_put(s_dir_pin, gpio_val);
    s_dir_forward = forward;
}

uint32_t stepgen_set_speed_hz(uint32_t hz) {
    if (!s_initialised || hz == 0) {
        s_current_speed_hz = 0;
        return 0;
    }
    uint32_t actual = stepgen_set_frequency(s_pio, s_sm, hz);
    s_current_speed_hz = actual;
    return actual;
}

void stepgen_move(int32_t steps, uint32_t speed_hz) {
    if (!s_initialised || steps == 0) return;

    // Cancel any active ramp (constant-speed move)
    s_ramp_phase = RampPhase::IDLE;

    // Set direction from sign
    stepgen_set_dir(steps > 0);

    // Set speed
    stepgen_set_speed_hz(speed_hz);

    // Set remaining count (IRQ handler will stop the SM when it reaches 0)
    s_remaining     = abs(steps);
    s_move_complete = false;
    s_running       = true;

    // Enable the SM
    stepgen_pio_start(s_pio, s_sm);
}

void stepgen_move_accel(int32_t steps, uint32_t max_speed_hz, uint32_t accel_hz_per_s) {
    if (!s_initialised || steps == 0) return;

    // No accel requested — fall back to constant speed
    if (accel_hz_per_s == 0) {
        stepgen_move(steps, max_speed_hz);
        return;
    }

    // Set direction from sign
    stepgen_set_dir(steps > 0);

    // Initialise ramp state
    s_ramp_speed = RAMP_MIN_HZ;
    s_ramp_max   = (float)max_speed_hz;
    s_ramp_accel = (float)accel_hz_per_s;
    s_ramp_phase = RampPhase::ACCEL;

    // Set initial PIO speed to the ramp start speed
    stepgen_set_frequency(s_pio, s_sm, (uint32_t)RAMP_MIN_HZ);
    s_current_speed_hz = (uint32_t)RAMP_MIN_HZ;

    // Set remaining count — PIO IRQ auto-stops at zero
    s_remaining     = abs(steps);
    s_move_complete = false;
    s_running       = true;

    // Go!
    stepgen_pio_start(s_pio, s_sm);
}

void stepgen_run(uint32_t speed_hz) {
    if (!s_initialised) return;

    // Cancel any active ramp (continuous constant-speed)
    s_ramp_phase = RampPhase::IDLE;

    stepgen_set_speed_hz(speed_hz);

    // -1 = infinite / continuous mode
    s_remaining     = -1;
    s_move_complete = false;
    s_running       = true;

    stepgen_pio_start(s_pio, s_sm);
}

void stepgen_stop() {
    if (!s_initialised) return;

    s_ramp_phase = RampPhase::IDLE;
    stepgen_pio_stop(s_pio, s_sm);
    s_running       = false;
    s_remaining     = 0;
    s_current_speed_hz = 0;
}

bool stepgen_is_busy() {
    return s_running;
}

int32_t stepgen_get_position() {
    return s_position;
}

void stepgen_reset_position() {
    s_position = 0;
}

void stepgen_set_position(int32_t pos) {
    s_position = pos;
}

uint32_t stepgen_get_speed_hz() {
    return s_current_speed_hz;
}

bool stepgen_get_dir() {
    return s_dir_forward;
}

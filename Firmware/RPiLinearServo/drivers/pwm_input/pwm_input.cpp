// PIO-based RC PWM pulse-width capture.
// Uses PIO1 / SM1 (PIO1/SM0 is WS2812, PIO0/SM0 is stepgen).

#include "pwm_input.h"
#include "config.h"
#include "pwm_capture.pio.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

// PIO instance
static PIO  s_pio = pio1;
static uint s_sm  = 1;          // SM1 (SM0 is WS2812)

// State
static uint32_t        s_last_us       = 0;     // latest valid pulse (µs)
static bool            s_valid         = false;  // last pulse in range?
static absolute_time_t s_last_valid_at;          // timestamp of last valid pulse
static bool            s_ever_valid    = false;  // have we ever seen a valid pulse?

// Precomputed conversion factor: µs = count * counts_to_us
// The PIO count loop is 2 instructions per count.
// pulse_us = count * 2 / (clk_sys / 1e6)  =  count * 2e6 / clk_sys
static float s_counts_to_us = 0.0f;

// Public API ─────────────────────────────────────────────────────────

void pwm_input_init(uint pin) {
    uint offset = pio_add_program(s_pio, &pwm_capture_program);
    pwm_capture_program_init(s_pio, s_sm, offset, pin);
    s_last_valid_at = get_absolute_time();
    s_counts_to_us  = 2.0f / (clock_get_hz(clk_sys) / 1000000.0f);
}

void pwm_input_poll() {
    // Drain FIFO — keep the most recent reading
    uint32_t count  = 0;
    bool     got    = false;
    while (!pio_sm_is_rx_fifo_empty(s_pio, s_sm)) {
        count = pio_sm_get(s_pio, s_sm);
        got   = true;
    }
    if (!got) {
        // No new data — expire validity after timeout so downstream
        // code sees the clean true→false transition it expects.
        if (s_valid && s_ever_valid) {
            uint64_t elapsed = absolute_time_diff_us(s_last_valid_at,
                                                      get_absolute_time());
            if (elapsed > (uint64_t)g_config.pwm_timeout_ms * 1000)
                s_valid = false;
        }
        return;
    }

    // Convert PIO counts → microseconds
    float us_f  = count * s_counts_to_us;
    uint32_t us = (uint32_t)(us_f + 0.5f);

    // Validate against configured range ± margin
    uint32_t lo = g_config.pwm_min_us - g_config.pwm_valid_margin;
    uint32_t hi = g_config.pwm_max_us + g_config.pwm_valid_margin;

    if (us >= lo && us <= hi) {
        s_last_us       = us;
        s_valid         = true;
        s_last_valid_at = get_absolute_time();
        s_ever_valid    = true;
    } else {
        s_valid = false;
    }
}

uint32_t pwm_input_get_us() {
    return s_last_us;
}

bool pwm_input_is_valid() {
    return s_valid;
}

bool pwm_input_is_timed_out() {
    if (!s_ever_valid) return false;   // never had signal — not a "timeout"
    uint64_t elapsed = absolute_time_diff_us(s_last_valid_at, get_absolute_time());
    return elapsed > (uint64_t)g_config.pwm_timeout_ms * 1000;
}

bool pwm_input_ever_valid() {
    return s_ever_valid;
}

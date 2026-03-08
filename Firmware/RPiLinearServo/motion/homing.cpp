// ── homing.cpp ──────────────────────────────────────────────────────────
// Hardstop homing: drives into the mechanical end-stop for longer than
// the full stroke (home_margin × stroke), then backs off and zeros.
// The motor stalls against the hardstop while the PIO keeps issuing pulses;
// completion is detected when the step count is exhausted.

#include "homing.h"
#include "stepgen.h"
#include "config.h"
#include "pins.h"
#include "status_led.h"
#include "nvm_store.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <cstdio>

bool g_homed = false;

bool homing_run() {
    printf("[homing] hardstop homing (margin=%.0f%%)\n",
           g_config.home_margin * 100.0f);
    status_led_set(LedStatus::HOMING);

    // ── 1. Enable driver ───────────────────────────────────────────────
    gpio_put(PIN_EN, g_config.en_active_low ? 0 : 1);
    sleep_ms(50);

    // ── 2. Calculate motion parameters ────────────────────────────────
    int32_t  home_steps = static_cast<int32_t>(
                              g_config.stroke_mm * g_config.home_margin
                              * g_config.steps_per_mm());
    uint32_t home_hz    = g_config.home_speed_hz();
    uint32_t accel       = g_config.accel_hz_per_s();

    int32_t signed_steps = g_config.home_dir_negative ? -home_steps : home_steps;

    printf("[homing] driving %ld steps @ %lu Hz toward hardstop\n",
           (long)home_steps, (unsigned long)home_hz);

    stepgen_move_accel(signed_steps, home_hz, accel);

    // ── 3. Wait for move to complete ───────────────────────────────────
    while (stepgen_is_busy()) {
        status_led_update();
        sleep_ms(10);
    }

    printf("[homing] stall phase complete, counter = %ld\n",
           (long)stepgen_get_position());

    // ── 4. Back off ────────────────────────────────────────────────────
    int32_t backoff        = g_config.backoff_steps();
    int32_t backoff_signed = g_config.home_dir_negative ? backoff : -backoff;
    printf("[homing] backing off %ld steps\n", (long)backoff);

    stepgen_move_accel(backoff_signed, home_hz, accel);
    while (stepgen_is_busy()) {
        status_led_update();
        sleep_ms(10);
    }

    // ── 5. Set logical zero ────────────────────────────────────────────
    stepgen_reset_position();
    g_homed = true;
    printf("[homing] homed OK — position reset to 0\n");
    status_led_set(LedStatus::HOMING_DONE);
    // Persist homed state immediately
    NvmData d;
    d.homed          = true;
    d.position_steps = 0;
    nvm_save(d);
    return true;
}

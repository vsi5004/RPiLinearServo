// ── main.cpp ────────────────────────────────────────────────────────────
// RPiLinearServo — Stage 2 firmware entry point.
// Composite USB (CDC + MSC config drive), PWM servo, hardstop homing.
// Driver: TMC2209 SilentStepStick via STEP/DIR + single-wire UART.

#include "pins.h"
#include "config.h"
#include "stepgen.h"
#include "homing.h"
#include "cli.h"
#include "nvm_store.h"
#include "tmc2209.h"
#include "pwm_input.h"
#include "status_led.h"
#include "usb_stdio.h"
#include "msc_disk.h"
#include "config_store.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include <cstdio>

// ── Banner ─────────────────────────────────────────────────────────────
static void print_banner() {
    printf("\n");
    printf("════════════════════════════════════════════\n");
    printf("  RPiLinearServo  v%d.%d.%d\n",
           FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    printf("  Stage 1 — Stepper exercise & homing\n");
    printf("  Driver: TMC2209 SilentStepStick\n");
    printf("════════════════════════════════════════════\n");
    printf("  sys_clk  : %lu MHz\n",
           (unsigned long)(clock_get_hz(clk_sys) / 1000000));
    printf("  STEP=GP%d  DIR=GP%d  EN=GP%d\n", PIN_STEP, PIN_DIR, PIN_EN);
    printf("  UART_TX=GP%d (via R1)  UART_RX=GP%d\n",
           PIN_UART_TX, PIN_UART_RX);
    printf("  PWM_IN=GP%d  range=%lu–%lu µs\n",
           PIN_PWM_IN, (unsigned long)g_config.pwm_min_us,
           (unsigned long)g_config.pwm_max_us);
    printf("  steps/mm=%.0f  stroke=%.1f mm  microsteps=%u\n",
           g_config.steps_per_mm(), g_config.stroke_mm, g_config.microsteps);
    printf("  homing: hardstop (%.0f%% of stroke)\n",
           g_config.home_margin * 100.0f);
    printf("  Type 'help' for commands.\n");
    printf("════════════════════════════════════════════\n\n");
}

// ── Main ───────────────────────────────────────────────────────────────
int main() {
    // ── 1.  Init stdio (USB CDC + MSC composite) ────────────────────────
    usb_stdio_init();

    // Wait up to 2 s for a USB host to connect so the banner is visible.
    // If nobody connects, we proceed anyway.
    for (int i = 0; i < 20; i++) {
        if (usb_stdio_connected()) break;
        sleep_ms(100);
    }

    // ── 2.  GPIO setup ─────────────────────────────────────────────────
    // EN pin: start disabled (safe default)
    gpio_init(PIN_EN);
    gpio_set_dir(PIN_EN, GPIO_OUT);
    gpio_put(PIN_EN, g_config.en_active_low ? 1 : 0);  // disabled

    // On-board WS2812 RGB LED
    status_led_init(PIN_LED);

    // ── 3.  Init subsystems ────────────────────────────────────────────
    // Load user config from flash (before TMC2209 so currents are applied)
    config_load(g_config);

    // Initialise TMC2209 UART and push startup configuration (current,
    // microstepping, StealthChop) before enabling steps.
    tmc2209_init(PIN_UART_TX, PIN_UART_RX);
    tmc2209_configure(g_config);

    stepgen_init(PIN_STEP, PIN_DIR);

    // PWM input capture (PIO1/SM1)
    pwm_input_init(PIN_PWM_IN);

    // Load NVM — restore homed state and position if valid
    NvmData nvm;
    if (nvm_load(nvm)) {
        g_homed = nvm.homed;
        if (g_homed) {
            stepgen_set_position(nvm.position_steps);
            printf("[nvm] restored homed=%d  pos=%ld steps\n",
                   g_homed, (long)nvm.position_steps);
        }
    }

    // Init USB mass storage config drive
    msc_disk_init();

    // ── 4.  Banner & CLI ───────────────────────────────────────────────
    print_banner();
    cli_init();

    // ── 5.  Main loop ──────────────────────────────────────────────────
    bool was_busy = false;
    absolute_time_t idle_since = get_absolute_time();
    bool auto_disabled = false;
    LedStatus prev_led = LedStatus::OFF;
    bool pwm_was_valid  = false;
    bool pwm_homing_triggered = false;

    while (true) {
        // Poll the CLI
        cli_poll();

        // Poll USB MSC — apply config if host wrote CONFIG.INI
        if (msc_disk_poll()) {
            // Config changed via USB — re-apply driver settings
            tmc2209_configure(g_config);
        }

        // ── PWM input ──────────────────────────────────────────────
        pwm_input_poll();
        bool pwm_valid = pwm_input_is_valid();
        bool pwm_tout  = pwm_input_is_timed_out();

        // Rising edge: first valid pulse (or recovery from timeout)
        if (pwm_valid && !pwm_was_valid) {
            printf("[pwm] signal acquired (%lu µs)\n",
                   (unsigned long)pwm_input_get_us());
        }

        // Auto-home on first valid PWM (before any position tracking)
        if (pwm_valid && !g_homed && !pwm_homing_triggered
            && !stepgen_is_busy()) {
            printf("[pwm] not homed — starting auto-home\n");
            pwm_homing_triggered = true;
            homing_run();
        }

        // Position tracking: move toward PWM target when homed & idle
        if (pwm_valid && g_homed && !stepgen_is_busy()) {
            float target_mm  = g_config.pwm_to_mm(pwm_input_get_us());
            float current_mm = (float)stepgen_get_position()
                               / g_config.steps_per_mm();
            float error_mm   = target_mm - current_mm;

            if (error_mm > g_config.deadband_mm
                || error_mm < -g_config.deadband_mm) {
                int32_t delta_steps = (int32_t)(error_mm
                                               * g_config.steps_per_mm());
                gpio_put(PIN_EN, g_config.en_active_low ? 0 : 1);
                status_led_set(LedStatus::MOVING);
                auto_disabled = false;
                stepgen_move_accel(delta_steps,
                                   g_config.default_speed_hz(),
                                   g_config.accel_hz_per_s());
            }
        }

        // PWM timeout: disable motor if configured
        if (pwm_tout && pwm_was_valid && g_config.pwm_zero_disables) {
            printf("[pwm] signal lost — disabling motor\n");
            stepgen_stop();
            gpio_put(PIN_EN, g_config.en_active_low ? 1 : 0);
            status_led_set(LedStatus::IDLE);
            auto_disabled = true;
        }
        pwm_was_valid = pwm_valid;

        // Auto-transition LED: MOVING → HOLDING when motion completes
        bool busy = stepgen_is_busy();
        if (was_busy && !busy) {
            if (status_led_get() == LedStatus::MOVING)
                status_led_set(LedStatus::HOLDING);
        }
        was_busy = busy;

        // Reset idle timer on any transition into HOLDING (move complete,
        // manual enable, homing done, etc.)
        LedStatus cur_led = status_led_get();
        if (cur_led == LedStatus::HOLDING && prev_led != LedStatus::HOLDING) {
            idle_since = get_absolute_time();
            auto_disabled = false;
        }
        prev_led = cur_led;

        // Auto-disable motor after idle timeout
        if (g_config.auto_disable_ms > 0
            && !auto_disabled
            && !busy
            && status_led_get() == LedStatus::HOLDING) {
            uint32_t idle_ms = (uint32_t)(absolute_time_diff_us(
                idle_since, get_absolute_time()) / 1000);
            if (idle_ms >= g_config.auto_disable_ms) {
                gpio_put(PIN_EN, g_config.en_active_low ? 1 : 0);
                status_led_set(LedStatus::IDLE);
                auto_disabled = true;
                printf("[auto] motor disabled after %lu ms idle\n",
                       (unsigned long)g_config.auto_disable_ms);
            }
        }

        // Update LED flash patterns
        status_led_update();

        // Periodic NVM save (throttled — only when data changed)
        if (!busy) {
            nvm_save_if_needed(stepgen_get_position(), g_homed);
        }

        // Tight loop hint (saves power on idle)
        tight_loop_contents();
    }

    return 0;  // unreachable
}

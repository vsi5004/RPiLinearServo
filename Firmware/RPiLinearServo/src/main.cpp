// ── main.cpp ────────────────────────────────────────────────────────────
// RPiLinearServo — Stage 1 firmware entry point.
// PIO-driven stepper exercise + CLI + hardstop homing.
// Driver: TMC2209 SilentStepStick via STEP/DIR + single-wire UART.

#include "pins.h"
#include "config.h"
#include "stepgen.h"
#include "homing.h"
#include "cli.h"
#include "nvm_store.h"
#include "tmc2209.h"
#include "status_led.h"

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
    printf("  steps/mm=%.0f  stroke=%.1f mm  microsteps=%u\n",
           g_config.steps_per_mm(), g_config.stroke_mm, g_config.microsteps);
    printf("  homing: hardstop (%.0f%% of stroke)\n",
           g_config.home_margin * 100.0f);
    printf("  Type 'help' for commands.\n");
    printf("════════════════════════════════════════════\n\n");
}

// ── Main ───────────────────────────────────────────────────────────────
int main() {
    // ── 1.  Init stdio (USB CDC) ───────────────────────────────────────
    stdio_init_all();

    // Wait up to 2 s for a USB host to connect so the banner is visible.
    // If nobody connects, we proceed anyway.
    for (int i = 0; i < 20; i++) {
        if (stdio_usb_connected()) break;
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
    // Initialise TMC2209 UART and push startup configuration (current,
    // microstepping, StealthChop) before enabling steps.
    tmc2209_init(PIN_UART_TX, PIN_UART_RX);
    tmc2209_configure(g_config);

    stepgen_init(PIN_STEP, PIN_DIR);

    // Load NVM (stub — returns defaults)
    NvmData nvm;
    nvm_load(nvm);

    // ── 4.  Banner & CLI ───────────────────────────────────────────────
    print_banner();
    cli_init();

    // ── 5.  Main loop ──────────────────────────────────────────────────
    bool was_busy = false;
    absolute_time_t idle_since = get_absolute_time();
    bool auto_disabled = false;
    LedStatus prev_led = LedStatus::OFF;

    auto led_name = [](LedStatus s) -> const char* {
        switch (s) {
            case LedStatus::OFF:          return "OFF";
            case LedStatus::HOLDING:      return "HOLDING";
            case LedStatus::MOVING:       return "MOVING";
            case LedStatus::HOMING:       return "HOMING";
            case LedStatus::HOMING_DONE:  return "HOMING_DONE";
            case LedStatus::STALL_FAULT:  return "STALL_FAULT";
            case LedStatus::ERROR:        return "ERROR";
            default:                      return "?";
        }
    };

    while (true) {
        // Poll the CLI
        cli_poll();

        // Auto-transition LED: MOVING → HOLDING when motion completes
        bool busy = stepgen_is_busy();
        if (was_busy && !busy) {
            printf("[main] busy->idle  LED=%s\n", led_name(status_led_get()));
            if (status_led_get() == LedStatus::MOVING) {
                status_led_set(LedStatus::HOLDING);
                printf("[main] LED -> HOLDING\n");
            }
        }
        if (!was_busy && busy) {
            printf("[main] idle->busy  LED=%s\n", led_name(status_led_get()));
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
                status_led_set(LedStatus::OFF);
                auto_disabled = true;
                printf("[auto] motor disabled after %lu ms idle\n",
                       (unsigned long)g_config.auto_disable_ms);
            }
        }

        // Update LED flash patterns
        status_led_update();

        // Tight loop hint (saves power on idle)
        tight_loop_contents();
    }

    return 0;  // unreachable
}

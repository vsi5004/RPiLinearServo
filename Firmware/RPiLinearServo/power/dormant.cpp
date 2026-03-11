// RP2040 dormant sleep implementation.
// Enters XOSC dormant when PWM signal is lost and USB is not mounted.
// Wakes on rising edge on PIN_PWM_IN (GP0).

#include "dormant.h"
#include "config.h"
#include "pins.h"
#include "homing.h"
#include "position_store.h"
#include "status_led.h"
#include "stepgen.h"
#include "pwm_input.h"
#include "tmc2209.h"
#include "usb_stdio.h"
#include "msc_disk.h"
#include "hall_sensor.h"
#include "servo_loop.h"

#include "tusb.h"
#include "hardware/xosc.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include <cstdio>

bool dormant_try_enter() {
    // Gate: feature must be enabled
    if (!g_config.sleep_when_idle)
        return false;

    // Gate: USB host has the device mounted — stay awake for CLI / config drive
    if (tud_mounted())
        return false;

    // --- pre-sleep housekeeping ---

    // Force-save position (bypass throttle — we may not wake for a long time)
    int32_t saved_pos   = stepgen_get_position();
    bool    saved_homed = g_homed;
    {
        PositionState ps;
        ps.homed          = saved_homed;
        ps.position_steps = saved_pos;
        position_save(ps);
    }

    // Motor & LED off
    gpio_put(PIN_EN, EN_DISABLE);
    status_led_set(LedStatus::OFF);
    status_led_update();            // push final black pixel to WS2812

    // Disable PIO state machines (stepgen on PIO0/SM0, ws2812 on PIO1/SM0,
    // pwm_capture on PIO1/SM1) so they don't interfere during dormant entry.
    pio_sm_set_enabled(pio0, 0, false);
    pio_sm_set_enabled(pio1, 0, false);
    pio_sm_set_enabled(pio1, 1, false);

    // Clear PIO instruction memory so init functions can re-load programs.
    pio_clear_instruction_memory(pio0);
    pio_clear_instruction_memory(pio1);

    printf("[power] entering dormant — wake on PWM rising edge\n");

    // Allow UART TX to drain
    sleep_ms(10);

    // --- configure dormant wake source ---
    gpio_set_dormant_irq_enabled(PIN_PWM_IN, GPIO_IRQ_EDGE_RISE, true);

    // --- tear down PLLs (mandatory before dormant or the chip locks up) ---
    // Switch clk_sys and clk_ref to XOSC/ROSC so PLLs can be safely stopped.
    // clk_ref → XOSC directly
    clock_configure_undivided(clk_ref,
                    CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                    0,
                    XOSC_HZ);
    // clk_sys → clk_ref (XOSC)
    clock_configure_undivided(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                    0,
                    XOSC_HZ);

    pll_deinit(pll_sys);
    pll_deinit(pll_usb);

    // =========================================================
    //  DORMANT — execution stops here until GP0 rising edge
    // =========================================================
    xosc_dormant();
    // =========================================================
    //  WAKE — XOSC is running and stable, PLLs are off
    // =========================================================

    // Clear dormant wake IRQ
    gpio_set_dormant_irq_enabled(PIN_PWM_IN, GPIO_IRQ_EDGE_RISE, false);
    gpio_acknowledge_irq(PIN_PWM_IN, GPIO_IRQ_EDGE_RISE);

    // --- restore clocks ---
    // Bring USB PLL back first (set_sys_clock_pll uses it as an interim source)
    pll_init(pll_usb, PLL_USB_REFDIV,
             PLL_USB_VCO_FREQ_HZ, PLL_USB_POSTDIV1, PLL_USB_POSTDIV2);

    // Restore system clock (re-inits pll_sys, configures clk_ref/clk_sys/clk_peri)
    set_sys_clock_pll(PLL_SYS_VCO_FREQ_HZ, PLL_SYS_POSTDIV1, PLL_SYS_POSTDIV2);

    // --- re-initialise peripherals ---
    // USB
    usb_stdio_init();
    msc_disk_init();

    // Motor driver
    tmc2209_init(PIN_UART_TX, PIN_UART_RX);
    tmc2209_configure(g_config);

    // PIO-based peripherals (PIO memory was cleared above)
    stepgen_init(PIN_STEP, PIN_DIR);
    pwm_input_init(PIN_PWM_IN);
    status_led_init(PIN_LED);

    // Hall sensor (ADC, no PIO)
    if (g_config.use_hall_effect) {
        hall_sensor_init();
    }

    // Restore position directly (no flash read — avoids corrective motion
    // caused by stale position data; hall verification handles any true drift).
    if (saved_homed) {
        stepgen_set_position(saved_pos);
    }
    g_homed = saved_homed;

    // Re-init servo loop state
    servo_loop_init();

    printf("[power] woke from dormant\n");

    return true;
}

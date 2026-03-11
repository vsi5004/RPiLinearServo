#include "pins.h"
#include "config.h"
#include "stepgen.h"
#include "homing.h"
#include "cli.h"
#include "position_store.h"
#include "tmc2209.h"
#include "pwm_input.h"
#include "status_led.h"
#include "usb_stdio.h"
#include "msc_disk.h"
#include "config_store.h"
#include "hall_sensor.h"
#include "servo_loop.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include <cstdio>
#include <cmath>


static void print_banner() {
    printf("\n");
    printf("════════════════════════════════════════════\n");
    printf("  RPiLinearServo  v%d.%d.%d\n",
           FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
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


int main() {

    usb_stdio_init();

    gpio_init(PIN_EN);
    gpio_set_dir(PIN_EN, GPIO_OUT);
    gpio_put(PIN_EN, EN_DISABLE);

    status_led_init(PIN_LED);
    config_load(g_config);
    tmc2209_init(PIN_UART_TX, PIN_UART_RX);
    tmc2209_configure(g_config);
    stepgen_init(PIN_STEP, PIN_DIR);
    pwm_input_init(PIN_PWM_IN);

    PositionState pos;
    if (position_load(pos)) {
        g_homed = pos.homed;
        if (g_homed) {
            stepgen_set_position(pos.position_steps);
        }
    }

    hall_sensor_init();
    if (hall_sensor_enabled()) {
        hall_cal_load();
    }

    msc_disk_init();
    cli_init();
    servo_loop_init();

    bool banner_printed = false;

    while (true) {
        cli_poll();

        if (!banner_printed && usb_stdio_connected()) {
            print_banner();
            if (g_homed) {
                printf("[pos] restored homed=%d  pos=%ld steps\n",
                       g_homed, (long)stepgen_get_position());
            }
            banner_printed = true;
        }

        // Apply config changes from USB mass storage
        if (msc_disk_poll()) {
            tmc2209_configure(g_config);
        }

        // PWM tracking, post-move verification, auto-disable, hall logging
        servo_loop_poll();
        status_led_update();

        if (!stepgen_is_busy()) {
            position_save_if_needed(stepgen_get_position(), g_homed);
        }

        tight_loop_contents();
    }

    return 0;  // unreachable
}

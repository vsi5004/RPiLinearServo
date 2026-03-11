// WS2812 RGB status LED — non-blocking flash patterns & timed transitions.

#include "status_led.h"
#include "ws2812.h"
#include "config.h"

#include "pico/stdlib.h"

static LedStatus       s_status    = LedStatus::OFF;
static absolute_time_t s_state_entered;      // timestamp of last status_led_set()
static uint8_t         s_flash_count = 0;    // for HOMING_DONE flash counter

// Track last-sent pixel to avoid flooding the WS2812 protocol
// (the LED needs >50 µs of idle/low between frames to latch).
static uint32_t s_last_rgb = 0;

static void led_set(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    if (rgb != s_last_rgb) {
        ws2812_set_rgb(r, g, b);
        s_last_rgb = rgb;
    }
}

static void led_off()        { led_set(0,   0,   0);   }
static void led_green()      { led_set(0,   32,  0);   }
static void led_blue()       { led_set(0,   0,   32);  }
static void led_red()        { led_set(32,  0,   0);   }


// Milliseconds elapsed since s_state_entered.
static uint32_t ms_in_state() {
    return (uint32_t)(absolute_time_diff_us(s_state_entered, get_absolute_time()) / 1000);
}

// Compute TPOWERDOWN delay in ms from config:
//   delay_s = tpowerdown × 2^18 / fclk_tmc
// TMC2209 internal oscillator ≈ 12 MHz.
static uint32_t tpowerdown_ms() {
    return (uint32_t)((uint64_t)g_config.tpowerdown * 262144ULL / 12000ULL);
}

// Public API

void status_led_init(uint pin) {
    ws2812_init(pin);
    s_status = LedStatus::OFF;
    s_state_entered = get_absolute_time();
    led_off();
}

void status_led_set(LedStatus s) {
    if (s == s_status) return;
    s_status = s;
    s_state_entered = get_absolute_time();
    s_flash_count = 0;
    s_last_rgb = 0xFFFFFFFF;  // force next led_set() to write
}

LedStatus status_led_get() {
    return s_status;
}

void status_led_update() {
    if (g_config.led_dark_mode) {
        led_off(); return;
    }

    uint32_t elapsed = ms_in_state();

    switch (s_status) {

    case LedStatus::OFF: {
        led_off();
        break;
    }

    case LedStatus::IDLE: {
        // Brief amber pulse once every 5 s (500 ms fade up/down)
        uint32_t phase = elapsed % 5000;
        uint32_t bright = 0;
        if (phase < 250)
            bright = (phase * 32) / 250;            // ramp up
        else if (phase < 500)
            bright = ((500 - phase) * 32) / 250;    // ramp down
        if (bright > 32) bright = 32;
        // Amber ≈ (R, R*0.4, 0)
        led_set((uint8_t)bright, (uint8_t)(bright * 2 / 5), 0);
        break;
    }

    case LedStatus::HOLDING: {
        led_green();
        break;
    }

    case LedStatus::MOVING: {
        led_blue();
        break;
    }

    case LedStatus::HOMING: {
        // Breathing blue: triangle wave 0→32→0 over 500 ms cycle
        uint32_t phase = elapsed % 500;               // 0..499
        uint32_t brightness;
        if (phase < 250)
            brightness = (phase * 32) / 250;           // ramp up
        else
            brightness = ((500 - phase) * 32) / 250;   // ramp down
        if (brightness > 32) brightness = 32;
        led_set(0, 0, (uint8_t)brightness);
        break;
    }

    case LedStatus::HOMING_DONE: {
        // 3× green flash (100 ms on / 100 ms off), then hold green
        // until TPOWERDOWN delay elapses, then transition to HOLDING.
        uint32_t flash_period = 200;  // 100 on + 100 off
        uint32_t flash_phase  = elapsed / flash_period;
        if (flash_phase < 3) {
            bool on = (elapsed % flash_period) < 100;
            on ? led_green() : led_off();
        } else if (elapsed < tpowerdown_ms()) {
            led_green();
        } else {
            // TPOWERDOWN elapsed — driver has ramped to hold current
            s_status = LedStatus::HOLDING;
            s_state_entered = get_absolute_time();
            s_last_rgb = 0xFFFFFFFF;  // force write on state change
            led_green();
        }
        break;
    }

    case LedStatus::STALL_FAULT: {
        // Rapid flash red: 100 ms on, 100 ms off
        bool on = (elapsed / 100) % 2 == 0;
        on ? led_red() : led_off();
        break;
    }

    case LedStatus::ERROR:{
        led_red();
        break;
    }
}
}

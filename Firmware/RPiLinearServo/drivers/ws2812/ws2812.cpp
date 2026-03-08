// ── ws2812.cpp ──────────────────────────────────────────────────────────
// Low-level WS2812 RGB LED — PIO1 / SM0.

#include "ws2812.h"
#include "ws2812.pio.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"

static PIO  s_pio = pio1;
static uint s_sm  = 0;

void ws2812_init(uint pin) {
    uint offset = pio_add_program(s_pio, &ws2812_program);
    ws2812_program_init(s_pio, s_sm, offset, pin, 800000.0f);
}

void ws2812_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    // WS2812 expects GRB byte order, shifted left 8 bits (autopull at 24 bits).
    uint32_t grb = ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8);
    pio_sm_put_blocking(s_pio, s_sm, grb);
}

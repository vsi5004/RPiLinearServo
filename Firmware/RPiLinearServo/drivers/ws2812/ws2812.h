#pragma once

#include "pico/types.h"
#include <cstdint>

// Initialise WS2812 PIO driver on the given pin (uses PIO1/SM0).
void ws2812_init(uint pin);

// Send a single pixel colour.  Blocking if FIFO is full (instant for 1 LED).
void ws2812_set_rgb(uint8_t r, uint8_t g, uint8_t b);

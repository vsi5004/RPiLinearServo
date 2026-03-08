#pragma once

// ── status_led.h ────────────────────────────────────────────────────────
// WS2812 RGB status LED manager.
// Call status_led_update() from the main loop — it handles flash patterns
// and timed transitions non-blockingly.

#include "pico/types.h"

enum class LedStatus {
    OFF,            // LED dark
    IDLE,           // Motor disabled, waiting — breathing amber
    HOLDING,        // Enabled, idle — solid green
    MOVING,         // Moving to target — solid blue
    HOMING,         // Homing active — rapid flash blue
    HOMING_DONE,    // Homing complete — 3× green flash, then → HOLDING
    STALL_FAULT,    // Unexpected stall — rapid flash red
    ERROR,          // Fault (UART fail, etc.) — solid red
};

// Initialise WS2812 hardware on the given pin and set state to OFF.
void status_led_init(uint pin);

// Set the current LED status.  Takes effect on next status_led_update().
void status_led_set(LedStatus s);

// Get the current LED status.
LedStatus status_led_get();

// Must be called frequently from the main loop.
// Drives flash patterns and timed auto-transitions.
void status_led_update();

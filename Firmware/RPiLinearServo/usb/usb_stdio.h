#pragma once

// Thin CDC-to-stdio bridge for composite USB (replaces pico_stdio_usb).
// Call usb_stdio_init() instead of stdio_init_all().

#ifdef __cplusplus
extern "C" {
#endif

/// Initialise TinyUSB and register the CDC stdio driver.
void usb_stdio_init(void);

/// Returns true when a USB host has the CDC port open.
bool usb_stdio_connected(void);

#ifdef __cplusplus
}
#endif

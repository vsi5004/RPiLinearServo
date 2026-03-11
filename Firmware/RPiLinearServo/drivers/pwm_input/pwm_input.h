#pragma once

// PIO-based RC PWM pulse-width capture on a single GPIO.
// Measures the HIGH time of each PWM frame and validates it against
// the configured [min_us, max_us] range with a margin.

#include "pico/types.h"
#include <cstdint>

/// Initialise the PWM capture PIO program on PIO1/SM1.
/// @param pin  GPIO pin connected to the RC PWM signal.
void pwm_input_init(uint pin);

/// Poll the PIO FIFO for new measurements.  Call from the main loop.
/// Drains the FIFO, keeps the latest reading, and updates validity/timeout.
void pwm_input_poll();

/// @return Most recent valid pulse width in microseconds (0 if never valid).
uint32_t pwm_input_get_us();

/// @return true if the last pulse was within the valid range.
bool pwm_input_is_valid();

/// @return true if no valid pulse has been received for longer than timeout_ms.
bool pwm_input_is_timed_out();

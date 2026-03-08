#pragma once

#include <cstdint>
#include "pico/types.h"    // for uint

// ── Step Generator ─────────────────────────────────────────────────────
// PIO-based STEP pulse generator with IRQ-counted position tracking.
// Provides exact step counts (no drift) via PIO IRQ on every rising edge.

/// Initialise the PIO step generator.
/// Must be called once before any other stepgen function.
/// @param step_pin  GPIO for STEP output (directly driven by PIO)
/// @param dir_pin   GPIO for DIR output  (driven by CPU)
void stepgen_init(uint step_pin, uint dir_pin);

/// Set direction.  Must be called while SM is stopped, or before
/// stepgen_move() / stepgen_run().  The PIO program does not touch DIR —
/// this simply writes the GPIO.
/// @param forward  true = forward, false = reverse.
///                 Respects g_config.dir_invert.
void stepgen_set_dir(bool forward);

/// Set step frequency (Hz).  Can be called while running for smooth
/// speed changes.
/// @param hz  Desired step frequency.  Clamped to safe range.
/// @return    Actual frequency achieved after clock-divider quantisation.
uint32_t stepgen_set_speed_hz(uint32_t hz);

/// Move exactly |steps| steps.  Direction is set from the sign.
/// Non-blocking — returns immediately.  Poll stepgen_is_busy().
/// The PIO SM is automatically stopped when the count is reached.
/// @param steps     Signed step count (negative = reverse direction).
/// @param speed_hz  Step frequency in Hz.
void stepgen_move(int32_t steps, uint32_t speed_hz);

/// Move with trapezoidal acceleration/deceleration profile.
/// Ramps from min speed up to max_speed_hz, cruises, then decelerates
/// to stop at exactly |steps|.  Uses a 1 kHz timer to update the PIO
/// clock divider.  Falls back to constant speed if accel is 0.
/// @param steps          Signed step count.
/// @param max_speed_hz   Cruise speed in Hz.
/// @param accel_hz_per_s Acceleration in Hz/s (e.g. 40000).
void stepgen_move_accel(int32_t steps, uint32_t max_speed_hz, uint32_t accel_hz_per_s);

/// Begin continuous stepping at the given frequency.
/// Runs until stepgen_stop() is called.
/// @param speed_hz  Step frequency in Hz.
void stepgen_run(uint32_t speed_hz);

/// Stop stepping immediately.  STEP pin is forced low.
/// Position counter retains its value.
void stepgen_stop();

/// @return true if the SM is currently generating pulses.
bool stepgen_is_busy();

/// @return Exact step position (updated by PIO IRQ handler).
///         Positive = forward, negative = reverse.
int32_t stepgen_get_position();

/// Zero the position counter.
void stepgen_reset_position();

/// Set the position counter to an arbitrary value.
/// Used to restore position from NVM on boot.
void stepgen_set_position(int32_t pos);

/// @return Current step frequency in Hz (0 if stopped).
uint32_t stepgen_get_speed_hz();

/// @return true if the current direction is forward.
bool stepgen_get_dir();

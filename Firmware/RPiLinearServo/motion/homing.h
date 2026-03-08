#pragma once

// ── Homing ─────────────────────────────────────────────────────────────
// Time-based stall homing: drive into the hard stop for longer than the
// full stroke, then back off.  Position counter is reset to zero.
// Safe because the linear actuator is non-backdrivable and the hard stop
// is rated for stall current.

/// Run the homing sequence (blocking).
/// Enables driver, drives toward home, backs off, resets position.
/// @return true on success.
bool homing_run();

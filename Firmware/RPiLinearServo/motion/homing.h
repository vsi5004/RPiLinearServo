#pragma once

// Time-based stall homing: drive into the hard stop for longer than the
// full stroke, then back off.  Position counter is reset to zero.
// Safe because the linear actuator is non-backdrivable and the hard stop
// is rated for stall current.

/// true once homing_run() has completed successfully.
extern bool g_homed;

/// true when lost-step detection has triggered a stall fault.
extern bool g_stall_fault;

/// Run the homing sequence (blocking).
/// Enables driver, drives toward home, backs off, resets position.
/// Sets g_homed = true on success.
/// @return true on success.
bool homing_run();

#pragma once

/// Call after a move completes to verify position via hall sensor.
/// If deviation exceeds threshold, attempts a single correction drive.
/// Sets g_stall_fault on unrecoverable error.
void hall_verify_post_move();

/// Log hall sensor reading during an active move (call at ~250 Hz).
void hall_log_sample();

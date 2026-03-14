#pragma once

#include <cstdint>

/// Initialise servo loop state (call once before entering main loop).
void servo_loop_init();

/// Run one iteration of the servo control loop.
/// Handles PWM tracking, auto-home, post-move verification,
/// auto-disable, and hall logging.
void servo_loop_poll();

/// Start an accelerated move (used by CLI and PWM tracking).
void servo_loop_move(int32_t steps, uint32_t speed_hz, uint32_t accel_hz_per_s);

/// Start continuous stepping at constant speed.
void servo_loop_run(uint32_t speed_hz);

/// Stop any active move and hold position.
void servo_loop_stop();

/// Enable driver without moving (enter HOLDING).
void servo_loop_enable();

/// Disable driver and stop any active move.
void servo_loop_disable();

/// Run blocking homing sequence.
void servo_loop_home();

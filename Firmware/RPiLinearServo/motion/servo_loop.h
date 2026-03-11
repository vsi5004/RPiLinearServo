#pragma once

/// Initialise servo loop state (call once before entering main loop).
void servo_loop_init();

/// Run one iteration of the servo control loop.
/// Handles PWM tracking, auto-home, post-move verification,
/// auto-disable, and hall logging.
void servo_loop_poll();

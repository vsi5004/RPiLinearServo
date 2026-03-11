#pragma once

// Low-power dormant mode.
// When enabled via sleep_when_idle, the RP2040 enters dormant (XOSC off)
// after the PWM signal is lost and USB is not mounted.
// Wake source: rising edge on PIN_PWM_IN (GP0).

/// Check conditions and enter dormant if appropriate.
/// Returns true if the device just woke from dormant (caller should
/// reset servo loop state).
bool dormant_try_enter();

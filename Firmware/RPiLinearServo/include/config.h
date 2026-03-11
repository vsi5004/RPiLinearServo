#pragma once

#include <cstdint>

// Version injected by CMake from the latest git tag (e.g. v1.0.0).
// Fallback values used only when building outside CMake.
#ifndef FW_VERSION_MAJOR
#define FW_VERSION_MAJOR 1
#endif
#ifndef FW_VERSION_MINOR
#define FW_VERSION_MINOR 0
#endif
#ifndef FW_VERSION_PATCH
#define FW_VERSION_PATCH 0
#endif
#ifndef FW_GIT_HASH
#define FW_GIT_HASH "unknown"
#endif

// Compile-time defaults.  Overridden at boot by flash-backed CONFIG.INI.

struct ServoConfig {
    float    stroke_mm         = 8.4f;
    float    deadband_mm       = 0.03f;
    float    pos_mismatch_mm   = 0.5f;
    bool     invert_stroke     = false;

    float    full_steps_per_mm = 297.6f;   // motor: ~20000 steps / 8.4mm / 8 microsteps
    bool     dir_invert        = false;

    // TMC2209 UART configuration (applied at startup via single-wire UART)
    uint16_t run_current_ma    = 100;      // RMS run current in mA
    uint16_t hold_current_ma   = 50;       // hold current in mA (motor idle)
    uint8_t  microsteps        = 8;        // 1/8 matches MS1=MS2=GND; intpol=1 interpolates to 256
    bool     stealthchop_en    = true;     // true = StealthChop (quiet), false = SpreadCycle

    uint8_t  tpowerdown       = 46;       // TPOWERDOWN register: ~1s before IRUN→IHOLD ramp

    // Derived: scales automatically with microsteps
    float steps_per_mm() const { return full_steps_per_mm * microsteps; }

    float    max_speed_mm_s    = 80.0f;    // 25,000 Hz at 583 steps/mm (matched AccelStepper)
    float    default_speed_mm_s = 40.0f;   // ~12,500 Hz CLI default
    float    min_speed_mm_s    = 0.1f;
    float    max_accel_mm_s2   = 80.0f;    // 40,000 steps/s² (matched AccelStepper)
    uint32_t auto_disable_ms   = 2000;     // disable EN after idle (0 = never)

    bool     homing_enable     = true;
    bool     home_dir_negative = true;      // true = home toward negative
    float    home_speed_mm_s   = 10.7f;    // ~half of default move speed
    float    backoff_mm        = 0.2f;
    float    home_margin       = 1.1f;      // drive 110% of stroke into hardstop
    float    zero_offset_mm    = 0.0f;

    uint32_t pwm_min_us        = 1000;
    uint32_t pwm_max_us        = 2000;
    uint32_t pwm_timeout_ms    = 100;
    bool     pwm_zero_disables = true;
    uint32_t pwm_valid_margin  = 50;

    /// Map a validated pulse width (µs) to a position in [0, stroke_mm].
    float pwm_to_mm(uint32_t pulse_us) const {
        if (pulse_us <= pwm_min_us) return 0.0f;
        if (pulse_us >= pwm_max_us) return stroke_mm;
        return (float)(pulse_us - pwm_min_us) * stroke_mm
               / (float)(pwm_max_us - pwm_min_us);
    }

    bool     led_dark_mode      = false;   // true = LED stays off at all times

    bool     sleep_when_idle     = false;   // true = enter dormant mode when PWM lost and USB not mounted

    bool     use_hall_effect     = false;   // true = use magnetic encoder feedback

    float    adc_filter_alpha   = 1.0f;   // 1.0 = no filtering (64× oversample is sufficient)
    float    lost_step_threshold_mv = 30.0f;  // mV deviation to trigger stall fault

    uint32_t min_save_interval_s = 30;     // throttle: min seconds between flash writes

    // Derived convenience: speed in Hz = speed_mm_s * steps_per_mm()
    uint32_t default_speed_hz() const {
        return static_cast<uint32_t>(default_speed_mm_s * steps_per_mm());
    }
    uint32_t min_speed_hz() const {
        return static_cast<uint32_t>(min_speed_mm_s * steps_per_mm());
    }
    uint32_t max_speed_hz() const {
        return static_cast<uint32_t>(max_speed_mm_s * steps_per_mm());
    }
    uint32_t home_speed_hz() const {
        return static_cast<uint32_t>(home_speed_mm_s * steps_per_mm());
    }
    uint32_t accel_hz_per_s() const {
        return static_cast<uint32_t>(max_accel_mm_s2 * steps_per_mm());
    }
    int32_t  backoff_steps() const {
        return static_cast<int32_t>(backoff_mm * steps_per_mm());
    }
    int32_t  stroke_steps() const {
        return static_cast<int32_t>(stroke_mm * steps_per_mm());
    }
};

// Global config instance (compile-time defaults)
inline ServoConfig g_config;

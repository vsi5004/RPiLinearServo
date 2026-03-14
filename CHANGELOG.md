# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [v1.1.0] - 2026-03-14

### Changed
- Servo loop rewritten as an explicit state machine (IDLE / HOMING / MOVING /
  HOLDING) with a single `enter_state()` function controlling the EN pin and
  LED — eliminates all EN/LED desynchronisation bugs
- CLI commands (`move`, `run`, `stop`, `home`, `enable`, `disable`, `faultclr`)
  now go through the `servo_loop_*()` API instead of directly manipulating
  GPIO and LED state, ensuring consistent behaviour with PWM-driven moves
- When the PWM signal drops during a long move, the servo transitions directly
  from MOVING to IDLE on completion instead of lingering in HOLDING
- PWM valid margin widened from 50 µs to 150 µs so signals slightly outside
  the 1000–2000 µs window are accepted
- Stepper driver is no longer disabled mid-move when PWM times out; the
  `pwm_zero_disables` timeout now waits until the move finishes

### Removed
- `ramp` CLI command (superseded by the built-in trapezoidal acceleration
  profiler in `stepgen_move_accel`)

## [v1.0.0] - 2026-03-11

### Added
- PIO-based step pulse generation on PIO0/SM0 with IRQ-counted position
  tracking (no drift) and trapezoidal acceleration via 1 kHz timer
- TMC2209 stepper driver control over half-duplex UART1 with CRC8 validation,
  configurable run/hold current, microstepping, and StealthChop/SpreadCycle
- RC PWM input capture on PIO1/SM1 with 16 ns resolution (1000–2000 µs
  default range), configurable timeout, and deadband
- Servo loop: PWM-to-position tracking with auto-home on first valid pulse,
  post-move hall verification, and idle auto-disable
- Hardstop homing with hall-effect stall detection, configurable backoff, and
  128-entry V-curve calibration sweep
- Hall-effect sensor feedback (DRV5055) with 64× oversampled ADC, EMA filter,
  and lost-step detection/correction
- Composite USB device: CDC serial port (interactive CLI) + MSC virtual FAT12
  drive exposing editable `CONFIG.INI`
- Full interactive CLI with motion commands, diagnostics, and status reporting
- NVM persistence for position (dual-slot wear-leveled), configuration, and
  hall calibration LUT — all CRC32-protected in dedicated flash sectors
- WS2812 status LED with state-driven animations (idle breathing, holding,
  moving, homing, stall fault, error) and optional dark mode
- Dormant sleep mode: optional ultra-low-power XOSC dormant when PWM signal is
  lost and USB is not mounted, instant wake on next PWM rising edge
- Tag-based GitHub Actions CI/CD with automatic GitHub Releases, SHA256
  checksums, and flash instructions
- Git-tag-driven firmware versioning injected at build time via CMake
- 3D-printable mounting hardware: horizontal, vertical, and long-screw base
  variants plus actuator horn
- KiCad 8 PCB design with full BOM, Gerber outputs, and sourcing notes

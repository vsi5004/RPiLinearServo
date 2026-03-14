# Firmware — RPiLinearServo

C++17 firmware for the RP2040-Zero targeting the Pico SDK. Implements closed-loop stepper control, RC PWM tracking, USB configuration, and interactive CLI.

## Build

### Prerequisites

- [Pico SDK](https://github.com/raspberrypi/pico-sdk) v2.2.0 — set `PICO_SDK_PATH` environment variable
- ARM GCC toolchain (14.2 Rel1 or compatible)
- CMake ≥ 3.13
- Ninja (or Make)

### Build from source

```bash
cd Firmware/RPiLinearServo
mkdir -p build && cd build
cmake ..
ninja
```

This produces `RPiLinearServo.uf2` along with `.bin`, `.hex`, `.dis` (disassembly), and `.map` (symbol map) outputs.

### Flash

1. Hold **BOOTSEL** on the RP2040-Zero and plug in USB.
2. Drag `RPiLinearServo.uf2` onto the **RPI-RP2** drive.
3. The board reboots and enumerates as a composite USB device (CDC serial + MSC config drive).

Alternatively, use the **Run Project** task in VS Code to build and flash in one step.

### VS Code development

The project includes CMake presets and VS Code tasks for a streamlined workflow:

1. Set `PICO_SDK_PATH` in your environment (or in `.vscode/settings.json`).
2. Open the `Firmware/RPiLinearServo` folder in VS Code.
3. CMake Tools will auto-configure the build directory.
4. Use **Build** (Ctrl+Shift+B) and **Run Project** tasks.

## Architecture

### Main loop

The firmware runs a single-core bare-metal event loop (`main.cpp`). There is no RTOS — all work is cooperative:

```
main loop
  ├── cli_poll()                  USB command processing (non-blocking)
  ├── msc_disk_poll()             Detect CONFIG.INI writes → apply settings
  ├── servo_loop_poll()           Servo state machine (see below)
  ├── status_led_update()         WS2812 LED animation state machine
  └── position_save_if_needed()   Throttled NVM write (≤ once per 30 s)
```

### Servo loop state machine (`motion/servo_loop.cpp`)

All motor control — whether initiated by PWM input or the USB CLI — flows through a single state machine.  This guarantees that the driver enable pin, LED status, and idle timeout are always consistent regardless of the command source.

```
  ┌──────┐   first valid PWM   ┌────────┐  blocking   ┌─────────┐
  │ IDLE │ ─────────────────► │ HOMING │ ──────────► │ HOLDING │
  └──┬───┘                     └────────┘             └────┬────┘
     │  move cmd / PWM error                               │
     │  ┌────────┐  move complete   ┌─────────┐            │
     └►│ MOVING │ ────────────────►│ HOLDING │◄───────────┘
       └────────┘                   └────┬────┘
                                         │ auto_disable_ms timeout
                                         │ or PWM signal lost
                                         ▼
                                      ┌──────┐
                                      │ IDLE │
                                      └──────┘
```

**States:**

| State | EN pin | LED | Description |
|---|---|---|---|
| IDLE | Disabled | Amber breathing | Motor off, waiting for input or dormant entry |
| HOMING | Enabled | Blue breathing | Blocking hardstop homing in progress |
| MOVING | Enabled | Solid blue | Stepgen actively running toward a target |
| HOLDING | Enabled | Solid green | At target, idle timer counting toward auto-disable |

Transitions are evaluated once per poll in a single `switch` block.  The `enter_state()` helper is the **only** place that touches the EN pin or LED status, eliminating any possibility of EN/LED desynchronisation.

**Public API** (used by both CLI and PWM tracking):

| Function | Description |
|---|---|
| `servo_loop_move(steps, speed, accel)` | Start an accelerated finite move |
| `servo_loop_run(speed)` | Start continuous stepping |
| `servo_loop_stop()` | Stop and enter HOLDING |
| `servo_loop_enable()` | Enable driver (IDLE → HOLDING) |
| `servo_loop_disable()` | Disable driver (any → IDLE) |
| `servo_loop_home()` | Run blocking homing sequence |

### Interrupt / real-time layer

| Source | Handler | Purpose |
|---|---|---|
| PIO0 IRQ0 | Step counter ISR | Increment/decrement position on every STEP rising edge |
| 1 kHz repeating timer | Accel/decel ISR | Update PIO clock divider for trapezoidal speed profile |
| PIO1 RX FIFO | PWM capture | New pulse width measurement available |

## Modules

### Step Generation (`drivers/stepgen/`)

PIO0/SM0 runs a 4-cycle program that generates a square wave on the STEP pin and fires IRQ0 on each rising edge. The CPU IRQ handler maintains an exact position counter (no drift).

Speed is controlled by adjusting the PIO state machine clock divider:

$$f_{step} = \frac{f_{clk\_sys}}{4 \times clk\_div}$$

Key functions:
- `stepgen_move(steps, speed_hz)` — move exactly N steps at constant speed
- `stepgen_move_accel(steps, max_hz, accel_hz_per_s)` — trapezoidal acceleration profile via 1 kHz timer
- `stepgen_run(speed_hz)` — continuous stepping
- `stepgen_get_position()` — read current step count

### TMC2209 Driver (`drivers/tmc2209/`)

Communicates over hardware UART1 (115200 baud, 8N1) on a half-duplex single-wire bus. Reads and writes TMC2209 registers with CRC8 validation. The TX pin is tri-stated after each write to allow the driver to reply.

At startup the firmware writes the following registers:

| Parameter | Default | Description |
|---|---|---|
| `run_current_ma` | 100 | RMS run current (mA) |
| `hold_current_ma` | 50 | RMS hold current (mA) |
| `microsteps` | 8 | Microstepping divisor |
| `stealthchop_en` | true | StealthChop (quiet) mode |
| `tpowerdown` | 46 | ~1 s IRUN → IHOLD ramp |

- `GCONF = 0x1C0` — full software current control, PDN disabled, register-select microstepping, multistep filter.
- `CHOPCONF` — only MRES and interpolation bits are modified; OTP defaults for TBL / HSTRT / HEND are preserved.

All parameters are runtime-configurable via the USB config drive (`CONFIG.INI`).

### PWM Input (`drivers/pwm_input/`)

PIO1/SM1 measures the HIGH time of an RC servo pulse at 16 ns resolution (125 MHz system clock). Valid range is configurable (default 1000–2000 µs). A timeout fires if no valid pulse is received within `pwm_timeout_ms`.

### Hall Sensor (`drivers/hall_sensor/`)

Reads the DRV5055 analog output on ADC0 (GP26) with 64× oversampling across 5 accumulated reads per update. An optional EMA filter smooths the signal. Used for:
- Stall detection during homing (voltage derivative monitoring)
- Post-move position verification via 128-entry calibration LUT
- Lost-step correction (single attempt, then fault if still out of tolerance)

### Dormant Sleep (`power/dormant.cpp`)

When `sleep_when_idle` is enabled and the PWM signal is lost, the RP2040 enters XOSC dormant mode — halting the crystal oscillator and both PLLs for minimal power draw. The device wakes instantly on the next PWM rising edge (GP0).

Entry conditions (all must be true):
- `sleep_when_idle = true` in config
- PWM signal timed out and motor auto-disabled **or** no valid pulse received within 5 seconds of boot
- USB host not mounted (prevents sleeping while CLI / config drive is in use)

Pre-sleep sequence: force an unconditional position save to flash, stash position in RAM, disable motor and LED, disable all PIO state machines, clear PIO instruction memory, switch clocks to XOSC, shut down both PLLs.

Post-wake sequence: restore PLLs and system clock, re-initialise all peripherals (USB, TMC2209, PIO drivers, hall sensor), restore position from RAM (avoids stale-flash mismatches), re-init the servo loop.

### Servo Loop (`motion/`)

The top-level motion coordinator.  An explicit state machine (IDLE → HOMING → MOVING → HOLDING) polled from the main loop.  All motor commands — from both the USB CLI and PWM tracking — flow through the same `servo_loop_*` API, ensuring EN pin, LED, and timeout state are always consistent.

Key behaviours:
- **PWM tracking** — maps pulse width to a target position, applies deadband, and commands moves with acceleration
- **Auto-home** — triggers homing on the first valid PWM pulse if not already homed
- **Post-move verification** — after each move settles, compares the hall sensor reading against the calibration LUT and attempts a single correction if a mismatch is detected
- **Auto-disable** — disables the driver after `auto_disable_ms` of idle time (HOLDING → IDLE)
- **PWM signal loss** — if the PWM signal drops during a move, the servo transitions directly from MOVING to IDLE on completion rather than lingering in HOLDING

### Homing (`motion/homing.cpp`)

Hardstop homing sequence:

1. Drive `home_margin × stroke` steps into the end-stop at `home_speed_mm_s`
2. Monitor hall sensor: stall detected when voltage derivative falls below threshold for 5 consecutive samples (after 20% of stroke has been commanded)
3. Back off by `backoff_mm` with acceleration
4. Calibration sweep: slow pass across full stroke to build 128-entry V-curve LUT
5. Reset position to 0, save LUT to flash

### USB (`usb/`)

Composite TinyUSB device with two interfaces:

- **CDC** — USB serial port for the interactive CLI
- **MSC** — virtual FAT12 mass storage drive (8 KB RAMdisk) exposing `CONFIG.INI`

The MSC driver detects write completions with a 500 ms idle timeout, then parses the INI, validates ranges, saves to flash, and re-applies driver settings.

### Storage (`storage/`)

All persistent data lives in dedicated 4 KB sectors at the end of the 2 MB flash:

| Offset | Purpose | Protection |
|---|---|---|
| `0x1F0000` | Position slot A | CRC32, dual-slot wear leveling |
| `0x1F1000` | Position slot B | CRC32, dual-slot wear leveling |
| `0x1F2000` | ServoConfig | CRC32, magic + version |
| `0x1F3000` | Hall calibration LUT | CRC32, magic + version |

Position saves are throttled to once every 30 seconds to protect flash endurance. An immediate save occurs after homing.

### Status LED (`ui/status_led.cpp`)

WS2812 state machine with the following visual states:

| State | Pattern |
|---|---|
| OFF | Dark (dark mode enabled) |
| IDLE | Amber breathing pulse (~5 s period) |
| HOLDING | Solid green |
| MOVING | Solid blue |
| HOMING | Breathing blue (500 ms cycle) |
| HOMING_DONE | 3× green flash → HOLDING |
| STALL_FAULT | Rapid red flash |
| ERROR | Solid red |

## Configuration Reference

The `ServoConfig` struct (`include/config.h`) contains all runtime parameters. Defaults are compiled in and can be overridden via the USB config drive or flash persistence.

| Section | Parameter | Default | Description |
|---|---|---|---|
| Stroke | `stroke_mm` | 8.40 | Actuator travel (mm) |
| | `full_steps_per_mm` | 208.3 | Motor full steps per mm |
| Driver | `dir_invert` | false | Invert direction pin |
| | `run_current_ma` | 100 | RMS run current (mA) |
| | `hold_current_ma` | 50 | RMS hold current (mA) |
| | `microsteps` | 8 | Microstepping divisor |
| | `stealthchop_en` | true | StealthChop mode |
| Motion | `default_speed_mm_s` | 40.0 | Default move speed (mm/s) |
| | `max_accel_mm_s2` | 80.0 | Acceleration limit (mm/s²) |
| | `auto_disable_ms` | 2000 | Idle timeout before disabling driver (0 = never) |
| RC PWM | `pwm_min_us` | 1000 | Minimum valid pulse width (µs) |
| | `pwm_max_us` | 2000 | Maximum valid pulse width (µs) |
| | `pwm_timeout_ms` | 100 | Signal-lost timeout (ms) |
| LED | `dark_mode` | false | Disable status LED |
| Power | `sleep_when_idle` | false | Enter dormant mode when PWM signal is lost (wake on next pulse) |
| Sensor | `use_hall_effect` | false | Enable hall sensor feedback |

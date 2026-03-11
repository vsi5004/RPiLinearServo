# RPiLinearServo

![RPiLinearServo](Docs/front_render.png)

A closed-loop linear servo based on the RP2040, using a micro stepper linear actuator with an 8.5 mm throw and a BTT v1.3 TMC2209 driver. Designed as an RC servo replacement / turnout actuator for model railroads that allows for smooth and reliable slow speed operation and ease of configuration for a wide range of applications.

![RPiLinearServo rear](Docs/rear_render.png)

## Features

- **Silent stepper control** — TMC2209 StealthChop with PIO-based step generation and trapezoidal acceleration
- **RC PWM input** — standard 1000–2000 µs servo signal mapped to linear position with 16 ns resolution
- **Auto-home on first PWM pulse** — hardstop homing with optional hall-effect stall detection
- **Hall-effect feedback** — DRV5055 sensor with 64× oversampled ADC and 128-entry calibration LUT for lost-step detection
- **USB Config Drive** — composite CDC + MSC device exposes an editable `CONFIG.INI` on a virtual FAT12 drive
- **Interactive USB CLI** — full command set for motion control, diagnostics, and configuration
- **NVM persistence** — position, configuration, and hall calibration survive power cycles (CRC32-protected flash sectors)
- **WS2812 status LED** — state-driven animations (idle, holding, moving, homing, fault) with optional dark mode
- **Immediate PWM response on boot** — no blocking USB wait

## Hardware

| Component | Details |
|---|---|
| MCU | Waveshare RP2040-Zero |
| Driver | BTT TMC2209 v1.3 SilentStepStick |
| Actuator | Micro stepper linear slide, 8.4 mm travel (~208.3 full steps/mm) |
| Hall Sensor | TI DRV5055 A2 (optional, for closed-loop verification) |

See the [PCB README](PCB/README.md) for the full schematic, BOM, and GPIO assignments.

## Getting Started

### Flash the firmware

1. Hold **BOOTSEL** on the RP2040-Zero and plug in USB.
2. Drag `RPiLinearServo.uf2` onto the **RPI-RP2** drive.
3. The board reboots and enumerates as a composite USB device (CDC serial + MSC config drive).

Pre-built `.uf2` files are available on the [Releases](../../releases) page — or build from source (see the [Firmware README](Firmware/RPiLinearServo/README.md)).

### Connect a PWM signal

Wire a standard RC servo signal (1000–2000 µs) to GP0. On the first valid pulse the servo automatically homes (drives into the hardstop, backs off, and zeroes) then begins tracking the PWM target.

If the PWM signal is lost for more than 100 ms (configurable), the motor is disabled and the LED enters an idle heartbeat.

### Configure via USB drive

The device mounts a small USB drive named **LINEARSERVO** containing `CONFIG.INI`. Edit with any text editor, save, and **safely eject**. The firmware applies changes within ~500 ms.

```ini
[stroke]
stroke_mm = 8.40
full_steps_per_mm = 208.3

[driver]
dir_invert = false
run_current_ma = 100
hold_current_ma = 50

[motion]
default_speed_mm_s = 40.0
max_accel_mm_s2 = 80.0
auto_disable_ms = 2000

[rc_pwm]
min_us = 1000
max_us = 2000

[led]
dark_mode = false

[sensor]
use_hall_effect = false
```

### USB CLI

Connect a serial terminal (PuTTY, minicom, `screen /dev/ttyACM0 115200`) to the CDC port. The CLI is always active alongside PWM control.

```
> help
Commands:
  move <steps> [speed_hz]  Move relative steps (negative = reverse)
  run [speed_hz]           Continuous stepping
  stop                     Stop immediately
  home                     Run homing sequence
  enable                   Enable motor driver
  disable                  Disable motor driver
  dir <fwd|rev>            Set direction for 'run'
  speed <hz>               Set default speed
  ramp <from> <to> <steps> Linear speed ramp
  pos                      Print position
  status                   Print full status
  pwm                      Print PWM input status
  nvm                      Print/save position state
  hall                     Hall sensor reading
  hallcal                  Dump hall calibration table
  faultclr                 Clear stall fault
  help                     This message
```

## Project Structure

| Directory | Description | Details |
|---|---|---|
| [Firmware/RPiLinearServo](Firmware/RPiLinearServo/README.md) | RP2040 firmware (C++17, Pico SDK) | Build instructions, architecture, module reference |
| [PCB](PCB/README.md) | KiCad 8 schematic & PCB | BOM, GPIO map, sourcing notes, Gerber outputs |
| [Mechanical](Mechanical/README.md) | 3D-printable mounting hardware | Mount variants, print settings, additional parts |
| [Docs](Docs/) | Renders and specifications | Project renders, design spec |

## License

TBD

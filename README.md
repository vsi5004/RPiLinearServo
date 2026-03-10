# RPiLinearServo

Closed-loop linear servo firmware for the RP2040, using a stepper-driven linear actuator and a TMC2209 SilentStepStick driver. Designed as an RC servo replacement / turnout actuator.

See [Docs/SPEC.md](Docs/SPEC.md) for the full specification.

## Current Stage — Stage 1+

- PIO-based step pulse generation with IRQ-counted position tracking (exact, no drift)
- Trapezoidal acceleration / deceleration via 1 kHz timer updating PIO clock divider
- TMC2209 UART configuration on startup (current, microstepping, StealthChop)
- Hardstop homing (drive 110 % of stroke into mechanical end-stop, back off, zero)
- WS2812 RGB status LED with breathing animation during homing
- Interactive CLI over USB CDC for exercising the stepper
- Auto-disable after configurable idle timeout
- RC PWM input via PIO capture — maps pulse width to linear position
- Auto-home on first valid PWM signal; position tracking while homed
- NVM persistence: homed flag and position survive power cycles (dual-slot flash with CRC32)
- LED status: idle heartbeat (amber), holding (green), moving (blue), homing (breathing blue), error (red)
- Dark mode option to disable LED entirely

## Hardware

| Component | Details |
|---|---|
| MCU | Waveshare RP2040-Zero |
| Driver | TMC2209 SilentStepStick |
| Actuator | Micro stepper linear slide, 8.4 mm travel (~208.3 full steps / mm) |

### GPIO Assignments

| Signal | RP2040-Zero | TMC2209 Pin | Notes |
|---|---|---|---|
| nENABLE | GP1 | EN | Active-low |
| STEP | GP2 | STEP | PIO0/SM0 driven |
| DIR | GP3 | DIR | GPIO |
| UART TX | GP4 | PDN/UART | Via 1 K series resistor on PCB |
| UART RX | GP5 | PDN/UART | Loopback on bus |
| INDEX | GP7 | INDEX | Unused (reserved) |
| DIAG | GP9 | DIAG | Unused (reserved) |
| PWM input | GP0 | — | RC servo signal via PIO1/SM1 |
| Hall sensor | GP26 | — | Analog ADC0 (future) |
| LED | GP16 | — | WS2812 RGB via PIO1/SM0 |

The 1 K series resistor between GP4 and the UART bus provides signal isolation so the TMC2209 can drive the bus low without fighting the RP2040 TX output during half-duplex reads. GP4 TX is tri-stated after each write to allow the TMC2209 to reply.

### TMC2209 Configuration

At startup the firmware writes the following registers over the single-wire UART:

| Parameter | Default | Description |
|---|---|---|
| `run_current_ma` | 100 | RMS run current (mA) |
| `hold_current_ma` | 50 | RMS hold current (mA) |
| `microsteps` | 8 | Microstepping divisor (MS1=MS2=GND hardware default) |
| `stealthchop_en` | true | StealthChop (quiet) mode |
| `tpowerdown` | 46 | TPOWERDOWN register (~1 s IRUN → IHOLD ramp) |

`GCONF = 0x1C0` — I_scale_analog off (full software current control), pdn_disable, mstep_reg_select, multistep_filt.
`CHOPCONF` is read-modify-write: only MRES and intpol bits are touched; OTP defaults for TBL / HSTRT / HEND are preserved.

### Homing

Hardstop homing drives the motor into the mechanical end-stop for 110 % of the configured stroke using the trapezoidal acceleration profile, then backs off and zeroes the position.

1. Enable driver, set LED to breathing blue
2. Drive `home_margin × stroke` steps toward the end-stop at `home_speed_mm_s`
3. Motor stalls against the hardstop; PIO keeps counting until all steps are issued
4. Back off by `backoff_mm` with acceleration
5. Reset position to 0, LED flashes green, then transitions to HOLDING

## PWM Input

The firmware senses a standard RC PWM signal (1000–2000 µs) on GP0 via PIO1/SM1 at 16 ns resolution. The pulse width is linearly mapped to a position across the configured stroke.

- **Auto-home**: on the first valid PWM pulse, the servo automatically homes before tracking
- **Position tracking**: while homed, the servo continuously follows the PWM target
- **Timeout**: if the PWM signal is lost for more than `pwm_timeout_ms` (default 100 ms), the motor is disabled and the LED enters the idle heartbeat
- **CLI coexistence**: the CLI remains active at all times alongside PWM control

## NVM Persistence

Homed state and motor position are saved to flash so the servo can resume after power loss without re-homing.

- **Dual-slot A/B**: two 4 KB sectors at the end of flash (0x1F0000, 0x1F1000) with a sequence counter
- **CRC32 validation**: each slot is verified on load; the highest valid sequence wins
- **Throttled saves**: writes occur at most once every 30 seconds to protect flash endurance
- **Immediate save on homing**: position is saved right after a successful home
- **CLI**: `nvm` command shows state, `nvm save` forces a write, `nvm clear` resets stored data

## Build

### Prerequisites

- [Pico SDK](https://github.com/raspberrypi/pico-sdk) v2.2.0
- ARM GCC toolchain (14.2 Rel1 or compatible)
- CMake ≥ 3.13, Ninja

### Local build

```bash
cd Firmware/RPiLinearServo
mkdir -p build && cd build
cmake ..
ninja
```

This produces `RPiLinearServo.uf2`.

### Flash

1. Hold BOOTSEL on the RP2040-Zero and plug in USB.
2. Drag `RPiLinearServo.uf2` onto the `RPI-RP2` drive.
3. The board reboots and enumerates as a USB CDC serial device.

Alternatively, flash via SWD with the **Flash** task in VS Code.

## USB CLI

Connect a serial terminal (PuTTY, minicom, `screen /dev/ttyACM0 115200`) to the CDC port.

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
  diag                     Print TMC2209 DRV_STATUS
  pwm                      Print PWM input status
  nvm [save|clear]         Show / save / clear NVM data
  wreg <addr> <value>      Write TMC2209 register (hex)
  rreg <addr>              Read TMC2209 register (hex)
  help                     This message
```

### Example session

```
> enable
driver enabled
> move 1000
move 1000 steps @ 46720 Hz  accel=93324 Hz/s
> pos
pos: 1000 steps  (0.857 mm)
> home
[homing] hardstop homing (margin=110%)
[homing] driving 15394 steps @ 12482 Hz toward hardstop
[homing] stall phase complete, counter = -15394
[homing] backing off 233 steps
[homing] homed OK — position reset to 0
> status
pos: 0 steps  (0.000 mm)
speed_hz: 46720  (default)
EN: enabled
> disable
driver disabled
```

## License

TBD

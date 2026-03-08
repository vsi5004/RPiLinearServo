# SPEC.md  
RP2040 Linear Servo Firmware Specification  
Target Board: Waveshare RP2040-Zero  

---

## 0. Overview

This firmware implements a closed-loop "linear servo" using an RP2040 and a linear stepper actuator. The device behaves like an RC servo replacement and optionally a turnout actuator.

Features:

- RC PWM input (standard servo pulse)
- Closed-loop position control using linear Hall sensor
- Automatic homing against mechanical stop
- USB Mass Storage (MSC) runtime configuration drive
- INI-based configuration file (`CONFIG.INI`)
- Persistent position and calibration storage in flash
- UF2 firmware update via BOOTSEL
- Optional USB CDC logging

Primary development stack: Pico SDK (C/C++) + TinyUSB.

---

## 1. Hardware Assumptions

### 1.1 MCU
- RP2040
- 2MB external QSPI flash
- USB device mode

### 1.2 Motor Driver

TMC2209 SilentStepStick

Interface:
- STEP / DIR / EN (STEP/DIR class — same as DRV8825)
- Single-wire UART for startup configuration (current, microstepping, StealthChop, StallGuard)
- DIAG output for StallGuard2 stall detection

GPIO assignments (Waveshare RP2040-Zero):

| Signal | GPIO | Direction | Notes |
|---|---|---|---|
| nENABLE | GP1 | OUT | Active-low |
| STEP | GP2 | OUT | PIO state machine (PIO0) |
| DIR | GP3 | OUT | GPIO |
| UART TX | GP4 | OUT | To TMC2209 PDN/UART via 1K R1 on PCB; also UART1_TX |
| UART RX | GP5 | IN | Loopback on same UART bus; also UART1_RX |
| DIAG | GP7 | IN | StallGuard2 output (high = stall detected) |
| INDEX | GP9 | IN | TMC2209 step pulse output (future use) |
| PWM input | GP0 | IN | RC servo signal |
| Hall sensor | GP26 | IN | Analog (ADC0) |

The UART is implemented with hardware UART1 (GP4=UART1_TX, GP5=UART1_RX) at 115200 baud 8N1. This leaves both PIO0 and PIO1 available for step generation, PWM capture and other future use.

### 1.3 Sensors
- Linear analog Hall sensor (e.g., A1302)
- Analog speed potentiometer
- RC PWM input

### 1.4 Mechanical
- Linear stepper actuator
- Non-backdrivable
- Hard end stop safe for stall homing

---

## 2. Functional Requirements

### 2.1 PWM Input

Standard RC servo PWM:
- 50Hz nominal
- 1000–2000 microseconds typical

Mapping:

```
pos_mm = (pulse_us - min_us) * stroke_mm / (max_us - min_us)
```

Clamped to [0, stroke_mm].

Configuration parameters:
- `min_us`
- `max_us`
- `timeout_ms`
- `pwm_zero_disables`
- `valid_margin_us`

Behavior:
- If no valid pulse for `timeout_ms`, PWM considered absent.
- If `pwm_zero_disables=true`, disable motor driver.
- Resume when valid PWM returns.
- If not homed and valid PWM appears → run homing first.

PWM capture must use PIO for deterministic measurement.

---

### 2.2 Homing

Triggered if:
- `homed=false`
- valid PWM received

Two homing modes (selected by `use_stallguard` config flag):

#### StallGuard homing (preferred, `use_stallguard = true`)

1. Configure TMC2209 StallGuard threshold via UART (`SGTHRS` register).
2. Move toward home direction at `home_speed_mm_s`.
3. Poll DIAG pin (GP7). When TMC2209 detects stall, DIAG goes high.
4. Call `stepgen_stop()` immediately — motor stops at contact point.
5. Back off by `backoff_mm`.
6. Set logical zero position.
7. Persist `homed=true`.

StallGuard is only active when the step rate exceeds `TCOOLTHRS` (set to maximum in firmware). Threshold tuning: increase `sg_threshold` if false triggers during normal motion; decrease if stall is not detected.

#### Time-based homing (fallback, `use_stallguard = false`)

1. Move toward home direction at `home_speed_mm_s`.
2. Drive 120% of stroke — motor stalls but PIO continues generating pulses.
3. Wait for step count to exhaust (all pulses issued).
4. Back off by `backoff_mm`.
5. Set logical zero position.

#### Hall latch (future)

When `use_latch = true`:
- After initial stall stop, approach slowly at `latch_speed_mm_s`
- Use Hall sensor position as zero reference

---

### 2.3 Closed Loop Control

Control loop frequency: 200–1000 Hz.

Inputs:
- Target position (from PWM)
- Current position (from Hall sensor)

Motion limits:
- `max_speed_mm_s`
- `min_speed_mm_s`
- `max_accel_mm_s2`
- `deadband_mm`

Speed potentiometer scales max speed:

```
effective_max_speed = min_speed + pot_norm * (max_speed - min_speed)
```

Stop stepping when error < deadband.

---

### 2.4 Disable Behavior

If PWM absent and `pwm_zero_disables=true`:
- Disable driver (EN pin)
- Stop control loop motion
- Assume position holds (non-backdrivable)

Persist position with throttling.

---

### 2.5 Boot Position Recovery

On boot:
- Load `homed` and `last_position_mm`
- Read Hall sensor

If mismatch > `pos_mismatch_mm`, trust Hall reading.

If not homed:
- Require homing before motion.

---

### 2.6 Nonvolatile Storage

Persist:
- `homed`
- `last_position_mm`
- Last-good config checksum
- Calibration data

Flash requirements:
- CRC protected
- Versioned struct
- A/B slot or ring buffer

Write throttling:
- Save only when disabled OR
- Every `pos_save_interval_s`
- Never more often than `min_save_interval_s`

---

## 3. USB Mass Storage Configuration Drive

### 3.1 USB Classes

- Required: MSC
- Recommended: MSC + CDC composite

### 3.2 Volume Layout

Volume label: `LINEARSERVO`

Files:
- `CONFIG.INI` (R/W)
- `STATUS.TXT` (RO)
- `README.TXT` (optional)

Disk size: 128–256 KB FAT volume
Backed by reserved flash region.

---

### 3.3 Config Apply Logic

On MSC write:
- Track if CONFIG.INI sectors modified.
- After `config_apply_idle_ms` (e.g. 500ms no writes):
  - Parse CONFIG.INI
  - If valid:
    - Apply
    - Persist as last-good
  - If invalid:
    - Reject
    - Write error to STATUS.TXT

---

## 4. CONFIG.INI Schema

Example:

```
[meta]
version = 1

[rc_pwm]
min_us = 1000
max_us = 2000
timeout_ms = 100
pwm_zero_disables = true
valid_margin_us = 50

[stroke]
length_mm = 12.0
invert = false
deadband_mm = 0.03
pos_mismatch_mm = 0.5

[motion]
max_speed_mm_s = 6.0
min_speed_mm_s = 0.2
max_accel_mm_s2 = 30.0
idle_disable_ms = 0

[homing]
enable = true
direction = negative
home_speed_mm_s = 1.0
latch_speed_mm_s = 0.3
use_latch = true
backoff_mm = 0.3
stall_eps_mm = 0.02
stall_time_ms = 100
zero_offset_mm = 0.0

[driver]
type = tmc2209
step_pin = GP2
dir_pin = GP3
en_pin = GP1
en_active_low = true
steps_per_mm = 200.0
dir_invert = false
run_current_ma = 800
hold_current_ma = 200
microsteps = 16
stealthchop_en = true
sg_threshold = 100
use_stallguard = true

[adc]
hall_adc = ADC0
pot_adc = ADC1
adc_filter_alpha = 0.1

[sense_hall]
mode = lut
mid_adc = 2048
lut_points = 13
lut = 1980,1990,2005,2020,...

[nvm]
pos_save_interval_s = 15
min_save_interval_s = 5
```

Unknown keys must be ignored.

---

## 5. Timing

### 5.1 PWM Capture
Use PIO state machine.
Measure high time in microseconds.
Provide timeout detection.

### 5.2 Step Generation
Generate STEP pulses meeting driver timing.
Prefer PIO or high-priority timer.
Minimum pulse width configurable.

TMC2209 STEP minimum high time: 100 ns.
At `STEPGEN_MIN_CLKDIV = 8.0` and 125 MHz: high time = 128 ns (meets spec with margin).

---

### 5.3 TMC2209 UART Protocol

Single-wire half-duplex UART, 115200 baud 8N1.
PCB: GP4 (TX) → 1K R1 → bus; GP5 (RX) direct on bus.
Implemented with hardware UART1 (GP4=UART1_TX, GP5=UART1_RX).

**Write datagram (8 bytes, master → TMC2209):**

| Byte | Value | Description |
|---|---|---|
| 0 | 0x05 | Sync nibble |
| 1 | 0x00 | Slave address (MS1=0, MS2=0) |
| 2 | reg \| 0x80 | Register address with write bit |
| 3–6 | data[31:0] | 32-bit data, MSByte first |
| 7 | CRC8 | CRC of bytes 0–6 |

**Read access (master sends 4 bytes, receives 8-byte reply):**

Master send: `[0x05, 0x00, reg, CRC8(3 bytes)]`

TMC2209 reply: `[0x05, 0xFF, reg, data[31:24..7:0], CRC8]`

Note: TX bytes echo back on RX bus. Firmware drains echo bytes before reading reply.

CRC: polynomial 0x07, initial value 0x00, LSB first shift.

**Registers written at startup:**

| Register | Address | Purpose |
|---|---|---|
| GCONF | 0x00 | pdn_disable=1 (required for UART), StealthChop/SpreadCycle |
| IHOLD_IRUN | 0x10 | Run and hold current (CS values 0–31) |
| TPWMTHRS | 0x13 | StealthChop upper speed threshold (0 = always active) |
| TCOOLTHRS | 0x14 | StallGuard lower speed threshold (0xFFFFF = always active) |
| CHOPCONF | 0x6C | Microstepping resolution (MRES field bits 27:24) |
| SGTHRS | 0x40 | StallGuard2 threshold (0–255) |

### 5.4 ADC
- IIR filter:

```
filtered = filtered + alpha * (raw - filtered)
```

Alpha from config.

---

## 6. Fault Handling

Faults:
- Hall out-of-range
- Config invalid
- Position beyond stroke limits

On fault:
- Disable driver
- Set error state
- Indicate via LED and STATUS.TXT

---

## 7. Flash Layout (2MB)

Example:

- 0x000000 – firmware
- mid region – reserved
- upper 128–256 KB – MSC disk
- final 32–64 KB – NVM storage

Flash sectors aligned to 4KB boundaries.

---

## 8. Firmware Structure

Modules:

- main.cpp
- config_ini.cpp
- usb_msc_disk.cpp
- pwm_capture_pio.cpp
- drivers/stepgen/stepgen.cpp     (PIO step generator, PIO0)
- drivers/tmc2209/tmc2209.cpp     (TMC2209 register driver, hardware UART1)
- motion/homing.cpp               (StallGuard and time-based homing)
- hall_sense.cpp
- nvm_store.cpp
- control.cpp

Produce:
- .elf
- .uf2

---

## 9. State Machine

States:

- BOOT
- WAIT_PWM
- HOMING
- RUN
- DISABLED
- ERROR

Transitions:
- PWM valid → RUN or HOMING
- PWM timeout → DISABLED
- Fault → ERROR

---

## 10. Priorities

1. Robustness over complexity
2. Safe flash handling
3. Clean separation of modules
4. Fail-safe motor disable on error
5. Minimal dependencies

---

End of SPEC.md
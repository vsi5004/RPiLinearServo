#pragma once

// TMC2209 SilentStepStick stepper driver (STEP/DIR interface)
constexpr int PIN_EN      = 1;   // GP1 — nENABLE output (active-low)
constexpr int PIN_STEP    = 2;   // GP2 — STEP output (PIO-driven)
constexpr int PIN_DIR     = 3;   // GP3 — DIR output

// nENABLE is active-low on all TMC2209 boards
static constexpr bool EN_ENABLE  = false;
static constexpr bool EN_DISABLE = true;

// TMC2209 UART — half-duplex, 1K series resistor R1 on PCB between TX and bus
// GP4/GP5 are also RP2040 UART1 TX/RX — PIO UART used for flexibility
constexpr int PIN_UART_TX = 4;   // GP4 — UART TX to TMC2209 PDN/UART (via R1)
constexpr int PIN_UART_RX = 5;   // GP5 — UART RX loopback from same bus

// TMC2209 diagnostic / StallGuard output
constexpr int PIN_DIAG    = 9;   // GP9 — DIAG output from TMC2209 (high = stall) [swapped with INDEX]

// TMC2209 INDEX output (position reference — future use)
constexpr int PIN_INDEX   = 7;   // GP7 — INDEX pulse output from TMC2209 [swapped with DIAG]

// Sensors and control inputs
constexpr int PIN_PWM_IN  = 0;   // GP0 — RC PWM servo input
constexpr int PIN_HALL    = 26;  // GP26 — Hall effect sensor analog output (ADC0)

// On-board LED (RP2040-Zero uses GP16 for WS2812 RGB LED)
constexpr int PIN_LED     = 16;  // GP16 — WS2812 LED data (used as simple GPIO here)

#pragma once

// TMC2209 SilentStepStick driver — hardware UART1 register access + startup config.
//
// PCB wiring (RP2040-Zero):
//   GP4 → [R1 1K] → TMC2209 PDN/UART  (UART1_TX)
//   GP5 ──────────→ TMC2209 PDN/UART  (UART1_RX, loopback on same bus)
//
// Call sequence on boot:
//   tmc2209_init(PIN_UART_TX, PIN_UART_RX)
//   tmc2209_configure(g_config)

#include "config.h"
#include "pico/types.h"
#include <stdint.h>
#include <stdbool.h>

constexpr uint8_t TMC2209_REG_GCONF       = 0x00;
constexpr uint8_t TMC2209_REG_GSTAT       = 0x01;
constexpr uint8_t TMC2209_REG_IFCNT       = 0x02;
constexpr uint8_t TMC2209_REG_IHOLD_IRUN  = 0x10;
constexpr uint8_t TMC2209_REG_TPOWERDOWN  = 0x11;
constexpr uint8_t TMC2209_REG_TSTEP       = 0x12;
constexpr uint8_t TMC2209_REG_TPWMTHRS    = 0x13;
constexpr uint8_t TMC2209_REG_CHOPCONF    = 0x6C;
constexpr uint8_t TMC2209_REG_DRV_STATUS  = 0x6F;

// Initialise hardware UART1 (TX on tx_pin, RX on rx_pin).
// Returns true on success.
bool tmc2209_init(uint tx_pin, uint rx_pin);

// Write startup configuration derived from ServoConfig:
//   - Run/hold current (IHOLD_IRUN)
//   - Microstepping resolution (CHOPCONF MRES)
//   - StealthChop / SpreadCycle mode (GCONF)
//   - TPOWERDOWN
// Verifies GCONF write with a read-back.
// Returns true if UART is responding.
bool tmc2209_configure(const ServoConfig &cfg);

// Raw register access (slave address 0x00, 8-byte write datagram).
void     tmc2209_write_reg(uint8_t reg, uint32_t value);
uint32_t tmc2209_read_reg(uint8_t reg);

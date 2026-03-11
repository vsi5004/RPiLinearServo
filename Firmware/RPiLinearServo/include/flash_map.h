#pragma once

// Centralised flash sector map for all persistent storage.
// RP2040: 2 MB flash, XIP-mapped at 0x10000000.
//
//   Offset      Size   Use
//   0x1F0000    4 KB   NVM slot A  (position + homed, A/B wear-leveling)
//   0x1F1000    4 KB   NVM slot B
//   0x1F2000    4 KB   Config      (ServoConfig struct)
//   0x1F3000    4 KB   Hall cal    (calibration LUT)
//   0x1F4000+          (free)

#include <cstdint>

static constexpr uint32_t FLASH_XIP_BASE     = 0x10000000;
static constexpr uint32_t FLASH_TOTAL_SIZE    = 2 * 1024 * 1024;
static constexpr uint32_t FLASH_SECTOR_SZ     = 4096;

// All offsets relative to flash start (for flash_range_erase/program).
static constexpr uint32_t FLASH_OFF_NVM_A     = 0x1F0000;
static constexpr uint32_t FLASH_OFF_NVM_B     = 0x1F1000;
static constexpr uint32_t FLASH_OFF_CONFIG    = 0x1F2000;
static constexpr uint32_t FLASH_OFF_HALL_CAL  = 0x1F3000;

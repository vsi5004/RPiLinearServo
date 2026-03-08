#pragma once

#include <cstdint>

// ── Nonvolatile Storage ────────────────────────────────────────────────
// Persistent storage for homing state, last position, and config CRC.
// Stage 1: stub implementation — interface defined, flash writes deferred.

// ── Flash layout (2 MB flash) ──────────────────────────────────────────
// Final 64 KB reserved for NVM.  Two 4 KB slots (A/B) for wear-leveling
// and corruption recovery.
//
//   0x1F0000  Slot A  (4 KB)
//   0x1F1000  Slot B  (4 KB)
//   ...       remaining reserved space
//
static constexpr uint32_t NVM_FLASH_BASE      = 0x10000000;  // XIP base
static constexpr uint32_t NVM_FLASH_SIZE       = 2 * 1024 * 1024;
static constexpr uint32_t NVM_REGION_SIZE      = 64 * 1024;
static constexpr uint32_t NVM_SECTOR_SIZE      = 4096;
static constexpr uint32_t NVM_FLASH_OFFSET_A   = NVM_FLASH_SIZE - NVM_REGION_SIZE;
static constexpr uint32_t NVM_FLASH_OFFSET_B   = NVM_FLASH_OFFSET_A + NVM_SECTOR_SIZE;

// ── Data struct ────────────────────────────────────────────────────────
static constexpr uint32_t NVM_VERSION = 1;
static constexpr uint32_t NVM_MAGIC  = 0x4C534E56;  // "LSNV" — Linear Servo NVM

struct NvmData {
    uint32_t magic;             // NVM_MAGIC
    uint32_t version;           // struct version for migration
    bool     homed;             // has the device been homed?
    int32_t  position_steps;    // last known position in steps
    uint32_t config_crc;        // CRC32 of last-good CONFIG.INI
    uint32_t data_crc;          // CRC32 of this struct (excluding this field)
};

// ── API ────────────────────────────────────────────────────────────────

/// Load NVM data from flash.
/// @param out  Filled with stored values, or defaults if not valid.
/// @return     true if valid data was loaded; false if defaults used.
bool nvm_load(NvmData &out);

/// Save NVM data to flash.
/// @return true on success.
bool nvm_save(const NvmData &data);

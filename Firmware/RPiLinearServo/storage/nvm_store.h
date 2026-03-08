#pragma once

#include <cstdint>

// ── Nonvolatile Storage ────────────────────────────────────────────────
// Persistent storage for homing state and last position.
// Dual-slot A/B with sequence counter for wear-leveling and corruption
// recovery.  CRC32 validates each slot on load.

// ── Flash layout (2 MB flash) ──────────────────────────────────────────
// Final 64 KB reserved for NVM.  Two 4 KB sectors (A/B) alternate writes.
//
//   0x1F0000  Slot A  (4 KB sector)
//   0x1F1000  Slot B  (4 KB sector)
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

struct __attribute__((packed)) NvmData {
    uint32_t magic;             // NVM_MAGIC
    uint32_t version;           // struct version for migration
    uint32_t sequence;          // monotonic counter — highest valid wins
    bool     homed;             // has the device been homed?
    int32_t  position_steps;    // last known position in steps
    uint32_t crc;               // CRC32 of all preceding fields
};

// ── API ────────────────────────────────────────────────────────────────

/// Load NVM data from flash (best of slot A / B).
/// @param out  Filled with stored values, or defaults if not valid.
/// @return     true if valid data was loaded; false if defaults used.
bool nvm_load(NvmData &out);

/// Save NVM data to flash (next slot in A/B rotation).
/// Automatically increments the sequence number.
/// @return true on success.
bool nvm_save(const NvmData &data);

/// Request a throttled save.  The actual write happens no sooner than
/// min_save_interval_s after the previous write.  Call frequently from
/// the main loop; it will only write when enough time has elapsed and
/// data has changed.
/// @param position_steps  Current step position.
/// @param homed           Current homed flag.
void nvm_save_if_needed(int32_t position_steps, bool homed);

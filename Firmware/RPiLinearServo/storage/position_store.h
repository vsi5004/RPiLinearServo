#pragma once

#include <cstdint>
#include "flash_map.h"

// Persistent storage for homing state and last stepper position.
// Dual-slot A/B with sequence counter for wear-leveling and corruption
// recovery.  CRC32 validates each slot on load.

static constexpr uint32_t POS_VERSION = 1;
static constexpr uint32_t POS_MAGIC   = 0x4C534E56;  // "LSNV" — Linear Servo NVM

struct __attribute__((packed)) PositionState {
    uint32_t magic;             // POS_MAGIC
    uint32_t version;           // struct version for migration
    uint32_t sequence;          // monotonic counter — highest valid wins
    bool     homed;             // has the device been homed?
    int32_t  position_steps;    // last known position in steps
    uint32_t crc;               // CRC32 of all preceding fields
};

// API

/// Load position state from flash (best of slot A / B).
/// @param out  Filled with stored values, or defaults if not valid.
/// @return     true if valid data was loaded; false if defaults used.
bool position_load(PositionState &out);

/// Save position state to flash (next slot in A/B rotation).
/// Automatically increments the sequence number.
/// @return true on success.
bool position_save(const PositionState &data);

/// Request a throttled save.  The actual write happens no sooner than
/// min_save_interval_s after the previous write.  Call frequently from
/// the main loop; it will only write when enough time has elapsed and
/// data has changed.
/// @param position_steps  Current step position.
/// @param homed           Current homed flag.
void position_save_if_needed(int32_t position_steps, bool homed);

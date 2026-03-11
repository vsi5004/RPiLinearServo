// Flash-backed persistent storage for stepper position and homing state.
// Dual-slot A/B wear-leveling with CRC32 validation.
// The slot with the highest valid sequence number is used on load.

#include "position_store.h"
#include "config.h"
#include "crc32.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

// Helpers

// Read a slot from XIP-mapped flash.
static bool read_slot(uint32_t flash_offset, PositionState &out) {
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(
        FLASH_XIP_BASE + flash_offset);
    std::memcpy(&out, ptr, sizeof(out));

    if (out.magic != POS_MAGIC || out.version != POS_VERSION)
        return false;

    // CRC covers everything up to (but not including) the crc field
    uint32_t expected = crc32(&out, offsetof(PositionState, crc));
    return out.crc == expected;
}

// Write a slot: erase sector, then program.
// Interrupts must be disabled while the flash hardware is active because
// XIP (execute-in-place) is suspended during erase/program.
static bool write_slot(uint32_t flash_offset, const PositionState &data) {
    // Prepare a page-aligned write buffer (flash programs 256-byte pages)
    uint8_t page[FLASH_PAGE_SIZE];
    std::memset(page, 0xFF, sizeof(page));
    std::memcpy(page, &data, sizeof(data));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, FLASH_SECTOR_SZ);
    flash_range_program(flash_offset, page, sizeof(page));
    restore_interrupts(ints);
    return true;
}

// Throttle state
static uint32_t        s_last_seq       = 0;
static bool            s_next_is_b      = false;  // which slot to write next
static absolute_time_t s_last_save_time;
static int32_t         s_saved_position  = 0;
static bool            s_saved_homed     = false;
static bool            s_initialised     = false;

// Public API

bool position_load(PositionState &out) {
    PositionState a, b;
    bool a_ok = read_slot(FLASH_OFF_NVM_A, a);
    bool b_ok = read_slot(FLASH_OFF_NVM_B, b);

    const PositionState *best = nullptr;

    if (a_ok && b_ok) {
        // Both valid — pick highest sequence (wrapping-safe comparison)
        best = ((int32_t)(b.sequence - a.sequence) > 0) ? &b : &a;
        s_next_is_b = (best == &a);  // write to the other slot
    } else if (a_ok) {
        best = &a;
        s_next_is_b = true;
    } else if (b_ok) {
        best = &b;
        s_next_is_b = false;
    }

    if (best) {
        out = *best;
        s_last_seq      = best->sequence;
        s_saved_position = best->position_steps;
        s_saved_homed    = best->homed;
        printf("[pos] loaded slot %c  seq=%lu  homed=%d  pos=%ld\n",
               (best == &a) ? 'A' : 'B',
               (unsigned long)best->sequence,
               best->homed, (long)best->position_steps);
    } else {
        // No valid data — return defaults
        std::memset(&out, 0, sizeof(out));
        out.magic    = POS_MAGIC;
        out.version  = POS_VERSION;
        out.sequence = 0;
        out.homed    = false;
        out.position_steps = 0;
        out.crc      = 0;
        printf("[pos] no valid data — using defaults\n");
    }

    s_last_save_time = get_absolute_time();
    s_initialised    = true;
    return best != nullptr;
}

bool position_save(const PositionState &data) {
    PositionState wr = data;
    wr.magic    = POS_MAGIC;
    wr.version  = POS_VERSION;
    wr.sequence = ++s_last_seq;
    wr.crc      = crc32(&wr, offsetof(PositionState, crc));

    uint32_t offset = s_next_is_b ? FLASH_OFF_NVM_B : FLASH_OFF_NVM_A;
    char slot_ch    = s_next_is_b ? 'B' : 'A';
    s_next_is_b     = !s_next_is_b;

    printf("[pos] saving slot %c  seq=%lu  homed=%d  pos=%ld\n",
           slot_ch, (unsigned long)wr.sequence,
           wr.homed, (long)wr.position_steps);

    bool ok = write_slot(offset, wr);

    s_saved_position = wr.position_steps;
    s_saved_homed    = wr.homed;
    s_last_save_time = get_absolute_time();
    return ok;
}

void position_save_if_needed(int32_t position_steps, bool homed) {
    if (!s_initialised) return;

    // Only save when something changed
    if (position_steps == s_saved_position && homed == s_saved_homed)
        return;

    // Throttle: respect min_save_interval_s
    uint64_t elapsed_us = absolute_time_diff_us(s_last_save_time,
                                                 get_absolute_time());
    if (elapsed_us < (uint64_t)g_config.min_save_interval_s * 1000000)
        return;

    PositionState d;
    d.homed          = homed;
    d.position_steps = position_steps;
    position_save(d);
}

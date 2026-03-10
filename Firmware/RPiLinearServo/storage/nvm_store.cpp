// ── nvm_store.cpp ───────────────────────────────────────────────────────
// Flash-backed persistent storage with dual-slot A/B wear-leveling.
// CRC32 validates each slot; the slot with the highest valid sequence
// number is used on load.

#include "nvm_store.h"
#include "config.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

// ── CRC32 (IEEE 802.3 polynomial, no lookup table) ─────────────────────
static uint32_t crc32(const void *buf, size_t len) {
    const uint8_t *p = static_cast<const uint8_t *>(buf);
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

// ── Helpers ────────────────────────────────────────────────────────────

// Read a slot from XIP-mapped flash.
static bool read_slot(uint32_t flash_offset, NvmData &out) {
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(
        NVM_FLASH_BASE + flash_offset);
    std::memcpy(&out, ptr, sizeof(out));

    if (out.magic != NVM_MAGIC || out.version != NVM_VERSION)
        return false;

    // CRC covers everything up to (but not including) the crc field
    uint32_t expected = crc32(&out, offsetof(NvmData, crc));
    return out.crc == expected;
}

// Write a slot: erase sector, then program.
// Interrupts must be disabled while the flash hardware is active because
// XIP (execute-in-place) is suspended during erase/program.
static bool write_slot(uint32_t flash_offset, const NvmData &data) {
    // Prepare a page-aligned write buffer (flash programs 256-byte pages)
    uint8_t page[FLASH_PAGE_SIZE];
    std::memset(page, 0xFF, sizeof(page));
    std::memcpy(page, &data, sizeof(data));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, NVM_SECTOR_SIZE);
    flash_range_program(flash_offset, page, sizeof(page));
    restore_interrupts(ints);
    return true;
}

// ── Throttle state ─────────────────────────────────────────────────────
static uint32_t        s_last_seq       = 0;
static bool            s_next_is_b      = false;  // which slot to write next
static absolute_time_t s_last_save_time;
static int32_t         s_saved_position  = 0;
static bool            s_saved_homed     = false;
static bool            s_initialised     = false;

// ── Public API ─────────────────────────────────────────────────────────

bool nvm_load(NvmData &out) {
    NvmData a, b;
    bool a_ok = read_slot(NVM_FLASH_OFFSET_A, a);
    bool b_ok = read_slot(NVM_FLASH_OFFSET_B, b);

    const NvmData *best = nullptr;

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
        printf("[nvm] loaded slot %c  seq=%lu  homed=%d  pos=%ld\n",
               (best == &a) ? 'A' : 'B',
               (unsigned long)best->sequence,
               best->homed, (long)best->position_steps);
    } else {
        // No valid data — return defaults
        std::memset(&out, 0, sizeof(out));
        out.magic    = NVM_MAGIC;
        out.version  = NVM_VERSION;
        out.sequence = 0;
        out.homed    = false;
        out.position_steps = 0;
        out.crc      = 0;
        printf("[nvm] no valid data — using defaults\n");
    }

    s_last_save_time = get_absolute_time();
    s_initialised    = true;
    return best != nullptr;
}

bool nvm_save(const NvmData &data) {
    NvmData wr = data;
    wr.magic    = NVM_MAGIC;
    wr.version  = NVM_VERSION;
    wr.sequence = ++s_last_seq;
    wr.crc      = crc32(&wr, offsetof(NvmData, crc));

    uint32_t offset = s_next_is_b ? NVM_FLASH_OFFSET_B : NVM_FLASH_OFFSET_A;
    char slot_ch    = s_next_is_b ? 'B' : 'A';
    s_next_is_b     = !s_next_is_b;

    printf("[nvm] saving slot %c  seq=%lu  homed=%d  pos=%ld\n",
           slot_ch, (unsigned long)wr.sequence,
           wr.homed, (long)wr.position_steps);

    bool ok = write_slot(offset, wr);

    s_saved_position = wr.position_steps;
    s_saved_homed    = wr.homed;
    s_last_save_time = get_absolute_time();
    return ok;
}

void nvm_save_if_needed(int32_t position_steps, bool homed) {
    if (!s_initialised) return;

    // Only save when something changed
    if (position_steps == s_saved_position && homed == s_saved_homed)
        return;

    // Throttle: respect min_save_interval_s
    uint64_t elapsed_us = absolute_time_diff_us(s_last_save_time,
                                                 get_absolute_time());
    if (elapsed_us < (uint64_t)g_config.min_save_interval_s * 1000000)
        return;

    NvmData d;
    d.homed          = homed;
    d.position_steps = position_steps;
    nvm_save(d);
}

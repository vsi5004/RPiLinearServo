// ── config_store.cpp ────────────────────────────────────────────────────
// Binary config persistence in a dedicated 4 KB flash sector.
// Uses magic + version + CRC32 for validation, similar to nvm_store.

#include "config_store.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

// ── Flash layout ───────────────────────────────────────────────────────
// NVM slots A/B at 0x1F0000 / 0x1F1000.  Config uses next sector.
static constexpr uint32_t CFG_FLASH_BASE    = 0x10000000;  // XIP base
static constexpr uint32_t CFG_FLASH_OFFSET  = 0x1F2000;    // 4 KB sector
static constexpr uint32_t CFG_SECTOR_SIZE   = 4096;

// ── On-disk struct ─────────────────────────────────────────────────────
static constexpr uint32_t CFG_MAGIC   = 0x4C534347;  // "LSCG"
static constexpr uint32_t CFG_VERSION = 1;

struct __attribute__((packed)) ConfigFlash {
    uint32_t magic;
    uint32_t version;

    // The 10 user-editable fields
    float    stroke_mm;
    uint8_t  dir_invert;        // bool stored as uint8
    uint16_t run_current_ma;
    uint16_t hold_current_ma;
    float    default_speed_mm_s;
    float    max_accel_mm_s2;
    uint32_t auto_disable_ms;
    uint32_t pwm_min_us;
    uint32_t pwm_max_us;
    uint8_t  led_dark_mode;     // bool stored as uint8

    uint32_t crc;               // CRC32 of all preceding fields
};

// ── CRC32 (same algorithm as nvm_store) ────────────────────────────────
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

// ── API ────────────────────────────────────────────────────────────────

bool config_load(ServoConfig &cfg) {
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(
        CFG_FLASH_BASE + CFG_FLASH_OFFSET);

    ConfigFlash f;
    memcpy(&f, ptr, sizeof(f));

    if (f.magic != CFG_MAGIC || f.version != CFG_VERSION)
        return false;

    uint32_t expected = crc32(&f, offsetof(ConfigFlash, crc));
    if (f.crc != expected) return false;

    // Apply to runtime config
    cfg.stroke_mm         = f.stroke_mm;
    cfg.dir_invert        = f.dir_invert != 0;
    cfg.run_current_ma    = f.run_current_ma;
    cfg.hold_current_ma   = f.hold_current_ma;
    cfg.default_speed_mm_s = f.default_speed_mm_s;
    cfg.max_accel_mm_s2   = f.max_accel_mm_s2;
    cfg.auto_disable_ms   = f.auto_disable_ms;
    cfg.pwm_min_us        = f.pwm_min_us;
    cfg.pwm_max_us        = f.pwm_max_us;
    cfg.led_dark_mode     = f.led_dark_mode != 0;

    printf("[cfg] loaded from flash: stroke=%.2f run_ma=%u\n",
           (double)cfg.stroke_mm, (unsigned)cfg.run_current_ma);
    return true;
}

bool config_save(const ServoConfig &cfg) {
    ConfigFlash f;
    memset(&f, 0, sizeof(f));
    f.magic   = CFG_MAGIC;
    f.version = CFG_VERSION;

    f.stroke_mm         = cfg.stroke_mm;
    f.dir_invert        = cfg.dir_invert ? 1 : 0;
    f.run_current_ma    = cfg.run_current_ma;
    f.hold_current_ma   = cfg.hold_current_ma;
    f.default_speed_mm_s = cfg.default_speed_mm_s;
    f.max_accel_mm_s2   = cfg.max_accel_mm_s2;
    f.auto_disable_ms   = cfg.auto_disable_ms;
    f.pwm_min_us        = cfg.pwm_min_us;
    f.pwm_max_us        = cfg.pwm_max_us;
    f.led_dark_mode     = cfg.led_dark_mode ? 1 : 0;

    f.crc = crc32(&f, offsetof(ConfigFlash, crc));

    // Write to flash (interrupts off)
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &f, sizeof(f));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CFG_FLASH_OFFSET, CFG_SECTOR_SIZE);
    flash_range_program(CFG_FLASH_OFFSET, page, sizeof(page));
    restore_interrupts(ints);

    printf("[cfg] saved to flash\n");
    return true;
}

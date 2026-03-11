#include "config_store.h"
#include "crc32.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

#include "flash_map.h"

// On-disk struct
static constexpr uint32_t CFG_MAGIC   = 0x4C534347;  // "LSCG"
static constexpr uint32_t CFG_VERSION = 4;

struct __attribute__((packed)) ConfigFlash {
    uint32_t magic;
    uint32_t version;

    // The 12 user-editable fields
    float    stroke_mm;
    float    full_steps_per_mm;
    uint8_t  dir_invert;        // bool stored as uint8
    uint16_t run_current_ma;
    uint16_t hold_current_ma;
    float    default_speed_mm_s;
    float    max_accel_mm_s2;
    uint32_t auto_disable_ms;
    uint32_t pwm_min_us;
    uint32_t pwm_max_us;
    uint8_t  led_dark_mode;     // bool stored as uint8
    uint8_t  use_hall_effect;   // bool stored as uint8
    float    lost_step_threshold_mv;

    uint32_t crc;               // CRC32 of all preceding fields
};

// API
bool config_load(ServoConfig &cfg) {
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(
        FLASH_XIP_BASE + FLASH_OFF_CONFIG);

    ConfigFlash f;
    memcpy(&f, ptr, sizeof(f));

    if (f.magic != CFG_MAGIC || f.version != CFG_VERSION)
        return false;

    uint32_t expected = crc32(&f, offsetof(ConfigFlash, crc));
    if (f.crc != expected) return false;

    // Apply to runtime config
    cfg.stroke_mm         = f.stroke_mm;
    cfg.full_steps_per_mm = f.full_steps_per_mm;
    cfg.dir_invert        = f.dir_invert != 0;
    cfg.run_current_ma    = f.run_current_ma;
    cfg.hold_current_ma   = f.hold_current_ma;
    cfg.default_speed_mm_s = f.default_speed_mm_s;
    cfg.max_accel_mm_s2   = f.max_accel_mm_s2;
    cfg.auto_disable_ms   = f.auto_disable_ms;
    cfg.pwm_min_us        = f.pwm_min_us;
    cfg.pwm_max_us        = f.pwm_max_us;
    cfg.led_dark_mode     = f.led_dark_mode != 0;
    cfg.use_hall_effect   = f.use_hall_effect != 0;
    cfg.lost_step_threshold_mv = f.lost_step_threshold_mv;

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
    f.full_steps_per_mm = cfg.full_steps_per_mm;
    f.dir_invert        = cfg.dir_invert ? 1 : 0;
    f.run_current_ma    = cfg.run_current_ma;
    f.hold_current_ma   = cfg.hold_current_ma;
    f.default_speed_mm_s = cfg.default_speed_mm_s;
    f.max_accel_mm_s2   = cfg.max_accel_mm_s2;
    f.auto_disable_ms   = cfg.auto_disable_ms;
    f.pwm_min_us        = cfg.pwm_min_us;
    f.pwm_max_us        = cfg.pwm_max_us;
    f.led_dark_mode     = cfg.led_dark_mode ? 1 : 0;
    f.use_hall_effect   = cfg.use_hall_effect ? 1 : 0;
    f.lost_step_threshold_mv = cfg.lost_step_threshold_mv;

    f.crc = crc32(&f, offsetof(ConfigFlash, crc));

    // Write to flash (interrupts off)
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &f, sizeof(f));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_OFF_CONFIG, FLASH_SECTOR_SZ);
    flash_range_program(FLASH_OFF_CONFIG, page, sizeof(page));
    restore_interrupts(ints);

    printf("[cfg] saved to flash\n");
    return true;
}

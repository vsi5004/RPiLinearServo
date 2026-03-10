// ── msc_disk.cpp ────────────────────────────────────────────────────────
// Virtual FAT12 USB mass storage disk for RPiLinearServo.
//
// Layout (16 × 512-byte blocks = 8 KB):
//   Block 0     Boot sector
//   Block 1     FAT12 table
//   Block 2     Root directory (16 entries)
//   Block 3-6   CONFIG.INI data (4 clusters = 2048 bytes max)
//   Block 7-8   STATUS.TXT data (2 clusters = 1024 bytes max)
//   Block 9-15  Unused
//
// CONFIG.INI is generated from g_config on init/refresh.
// When the host writes to CONFIG.INI blocks, we buffer the data and
// trigger a config-apply after a write-idle timeout.

#include "msc_disk.h"
#include "tusb.h"
#include "config.h"
#include "config_ini.h"
#include "config_store.h"

#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

// ── Disk geometry ──────────────────────────────────────────────────────
#define DISK_BLOCK_SIZE  512
#define DISK_BLOCK_NUM   16    // 8 KB total — smallest Windows will mount

// Block assignments
#define BLK_BOOT    0
#define BLK_FAT     1
#define BLK_ROOT    2
#define BLK_CFG     3   // CONFIG.INI starts here (clusters 2-5)
#define BLK_CFG_END 6   // inclusive
#define BLK_STS     7   // STATUS.TXT starts here (clusters 6-7)
#define BLK_STS_END 8   // inclusive

#define CFG_MAX_SIZE  ((BLK_CFG_END - BLK_CFG + 1) * DISK_BLOCK_SIZE)  // 2048
#define STS_MAX_SIZE  ((BLK_STS_END - BLK_STS + 1) * DISK_BLOCK_SIZE)  // 1024

// ── RAM disk image ─────────────────────────────────────────────────────
static uint8_t s_disk[DISK_BLOCK_NUM][DISK_BLOCK_SIZE];

// ── File content buffers ───────────────────────────────────────────────
static char s_cfg_buf[CFG_MAX_SIZE];   // CONFIG.INI text
static uint32_t s_cfg_len = 0;

static char s_sts_buf[STS_MAX_SIZE];   // STATUS.TXT text
static uint32_t s_sts_len = 0;

// ── Write tracking ─────────────────────────────────────────────────────
static bool s_cfg_dirty = false;
static absolute_time_t s_last_write_time;
static constexpr uint32_t WRITE_IDLE_MS = 500;

// ── FAT12 helpers ──────────────────────────────────────────────────────

// Put a 32-bit LE value at offset in a block
static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// Write a FAT12 entry.  FAT12 packs two 12-bit entries into 3 bytes.
static void fat12_set(uint8_t *fat, uint16_t cluster, uint16_t value) {
    uint32_t byte_off = cluster + (cluster / 2);
    if (cluster & 1) {
        fat[byte_off]     = (fat[byte_off] & 0x0F) | (uint8_t)((value & 0x0F) << 4);
        fat[byte_off + 1] = (uint8_t)(value >> 4);
    } else {
        fat[byte_off]     = (uint8_t)(value & 0xFF);
        fat[byte_off + 1] = (fat[byte_off + 1] & 0xF0) | (uint8_t)((value >> 8) & 0x0F);
    }
}

// Write an 8.3 directory entry
static void dir_entry(uint8_t *ent, const char *name83, uint8_t attr,
                      uint16_t cluster, uint32_t size) {
    memset(ent, 0, 32);
    memcpy(ent, name83, 11);
    ent[11] = attr;
    // Timestamps: 2025-01-01 00:00:00
    put_le16(ent + 14, 0x0000);  // create time
    put_le16(ent + 16, 0x5A21);  // create date
    put_le16(ent + 18, 0x5A21);  // access date
    put_le16(ent + 26, cluster); // first cluster
    put_le32(ent + 28, size);
}

// ── Build the disk image ───────────────────────────────────────────────
static void build_disk(void) {
    memset(s_disk, 0, sizeof(s_disk));

    // ── Block 0: Boot sector (BPB) ────────────────────────────────────
    uint8_t *boot = s_disk[BLK_BOOT];
    boot[0] = 0xEB; boot[1] = 0x3C; boot[2] = 0x90;  // JMP short
    memcpy(boot + 3, "MSDOS5.0", 8);                  // OEM name
    put_le16(boot + 11, DISK_BLOCK_SIZE);   // bytes per sector
    boot[13] = 1;                           // sectors per cluster
    put_le16(boot + 14, 1);                 // reserved sectors (boot)
    boot[16] = 1;                           // number of FATs
    put_le16(boot + 17, 16);                // root dir entries
    put_le16(boot + 19, DISK_BLOCK_NUM);    // total sectors 16-bit
    boot[21] = 0xF8;                        // media type (hard disk)
    put_le16(boot + 22, 1);                 // sectors per FAT
    put_le16(boot + 24, 1);                 // sectors per track
    put_le16(boot + 26, 1);                 // heads
    boot[38] = 0x29;                        // extended boot sig
    put_le32(boot + 39, 0x12345678);        // volume serial
    memcpy(boot + 43, "LINEARSERVO", 11);   // volume label (padded)
    memcpy(boot + 54, "FAT12   ", 8);       // filesystem type
    boot[510] = 0x55; boot[511] = 0xAA;    // boot signature

    // ── Block 1: FAT12 table ──────────────────────────────────────────
    uint8_t *fat = s_disk[BLK_FAT];
    fat[0] = 0xF8; fat[1] = 0xFF; fat[2] = 0xFF;  // media + reserved clusters 0,1

    // CONFIG.INI: clusters 2,3,4,5 (blocks 3,4,5,6)
    uint16_t cfg_clusters = (s_cfg_len > 0)
        ? (uint16_t)((s_cfg_len + DISK_BLOCK_SIZE - 1) / DISK_BLOCK_SIZE)
        : 1;
    if (cfg_clusters > 4) cfg_clusters = 4;
    for (uint16_t i = 0; i < cfg_clusters; i++) {
        uint16_t cluster = 2 + i;
        uint16_t next = (i + 1 < cfg_clusters) ? cluster + 1 : 0xFFF;
        fat12_set(fat, cluster, next);
    }

    // STATUS.TXT: clusters 6,7 (blocks 7,8)
    uint16_t sts_clusters = (s_sts_len > 0)
        ? (uint16_t)((s_sts_len + DISK_BLOCK_SIZE - 1) / DISK_BLOCK_SIZE)
        : 1;
    if (sts_clusters > 2) sts_clusters = 2;
    for (uint16_t i = 0; i < sts_clusters; i++) {
        uint16_t cluster = 6 + i;
        uint16_t next = (i + 1 < sts_clusters) ? cluster + 1 : 0xFFF;
        fat12_set(fat, cluster, next);
    }

    // ── Block 2: Root directory ───────────────────────────────────────
    uint8_t *root = s_disk[BLK_ROOT];

    // Entry 0: volume label
    memcpy(root, "LINEARSERVO", 11);
    root[11] = 0x08;  // volume label attribute

    // Entry 1: CONFIG.INI
    dir_entry(root + 32, "CONFIG  INI", 0x20, 2, s_cfg_len);

    // Entry 2: STATUS.TXT (read-only attribute)
    dir_entry(root + 64, "STATUS  TXT", 0x21, 6, s_sts_len);

    // ── Data blocks: CONFIG.INI content ───────────────────────────────
    if (s_cfg_len > 0) {
        uint32_t remaining = s_cfg_len;
        for (int blk = BLK_CFG; blk <= BLK_CFG_END && remaining > 0; blk++) {
            uint32_t chunk = remaining > DISK_BLOCK_SIZE ? DISK_BLOCK_SIZE : remaining;
            memcpy(s_disk[blk], s_cfg_buf + (blk - BLK_CFG) * DISK_BLOCK_SIZE, chunk);
            remaining -= chunk;
        }
    }

    // ── Data blocks: STATUS.TXT content ───────────────────────────────
    if (s_sts_len > 0) {
        uint32_t remaining = s_sts_len;
        for (int blk = BLK_STS; blk <= BLK_STS_END && remaining > 0; blk++) {
            uint32_t chunk = remaining > DISK_BLOCK_SIZE ? DISK_BLOCK_SIZE : remaining;
            memcpy(s_disk[blk], s_sts_buf + (blk - BLK_STS) * DISK_BLOCK_SIZE, chunk);
            remaining -= chunk;
        }
    }
}

// ── Generate CONFIG.INI from g_config ──────────────────────────────────
static void generate_config_ini(void) {
    int n = snprintf(s_cfg_buf, sizeof(s_cfg_buf),
        "; RPiLinearServo Configuration\r\n"
        "; Edit values below and safely eject to apply.\r\n"
        "\r\n"
        "[stroke]\r\n"
        "stroke_mm = %.2f\r\n"
        "\r\n"
        "[driver]\r\n"
        "dir_invert = %s\r\n"
        "run_current_ma = %u\r\n"
        "hold_current_ma = %u\r\n"
        "\r\n"
        "[motion]\r\n"
        "default_speed_mm_s = %.1f\r\n"
        "max_accel_mm_s2 = %.1f\r\n"
        "auto_disable_ms = %lu\r\n"
        "\r\n"
        "[rc_pwm]\r\n"
        "min_us = %lu\r\n"
        "max_us = %lu\r\n"
        "\r\n"
        "[led]\r\n"
        "dark_mode = %s\r\n",
        (double)g_config.stroke_mm,
        g_config.dir_invert ? "true" : "false",
        (unsigned)g_config.run_current_ma,
        (unsigned)g_config.hold_current_ma,
        (double)g_config.default_speed_mm_s,
        (double)g_config.max_accel_mm_s2,
        (unsigned long)g_config.auto_disable_ms,
        (unsigned long)g_config.pwm_min_us,
        (unsigned long)g_config.pwm_max_us,
        g_config.led_dark_mode ? "true" : "false"
    );
    s_cfg_len = (n > 0 && n < (int)sizeof(s_cfg_buf)) ? (uint32_t)n : 0;
}

// ── Generate STATUS.TXT ────────────────────────────────────────────────
static void generate_status_txt(void) {
    int n = snprintf(s_sts_buf, sizeof(s_sts_buf),
        "RPiLinearServo v%d.%d.%d\r\n"
        "Status: OK\r\n",
        FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH
    );
    s_sts_len = (n > 0 && n < (int)sizeof(s_sts_buf)) ? (uint32_t)n : 0;
}

// ── Public API ─────────────────────────────────────────────────────────

void msc_disk_init(void) {
    generate_config_ini();
    generate_status_txt();
    build_disk();
    s_cfg_dirty = false;
}

void msc_disk_refresh(void) {
    generate_config_ini();
    build_disk();
}

void msc_disk_set_status(const char *text) {
    size_t len = strlen(text);
    if (len >= sizeof(s_sts_buf)) len = sizeof(s_sts_buf) - 1;
    memcpy(s_sts_buf, text, len);
    s_sts_buf[len] = '\0';
    s_sts_len = (uint32_t)len;
    build_disk();
}

bool msc_disk_poll(void) {
    if (!s_cfg_dirty) return false;

    uint32_t elapsed = (uint32_t)(absolute_time_diff_us(
        s_last_write_time, get_absolute_time()) / 1000);
    if (elapsed < WRITE_IDLE_MS) return false;

    // Write-idle timeout reached — parse the buffered CONFIG.INI
    s_cfg_dirty = false;

    // Extract CONFIG.INI text from the disk blocks
    // Re-read from s_disk blocks (host may have written partial updates)
    char ini_buf[CFG_MAX_SIZE + 1];
    memset(ini_buf, 0, sizeof(ini_buf));
    for (int blk = BLK_CFG; blk <= BLK_CFG_END; blk++) {
        memcpy(ini_buf + (blk - BLK_CFG) * DISK_BLOCK_SIZE,
               s_disk[blk], DISK_BLOCK_SIZE);
    }
    // Find actual end of text (strip trailing zeros/padding)
    size_t ini_len = 0;
    for (size_t i = 0; i < CFG_MAX_SIZE; i++) {
        if (ini_buf[i] == '\0') break;
        ini_len = i + 1;
    }
    ini_buf[ini_len] = '\0';

    // Parse and apply
    char err_buf[256];
    bool ok = config_ini_parse(ini_buf, ini_len, g_config, err_buf, sizeof(err_buf));

    if (ok) {
        config_save(g_config);
        printf("[msc] config applied and saved\n");

        char sts[512];
        snprintf(sts, sizeof(sts),
            "RPiLinearServo v%d.%d.%d\r\n"
            "Config: Applied OK\r\n",
            FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
        msc_disk_set_status(sts);
    } else {
        printf("[msc] config error: %s\n", err_buf);

        char sts[512];
        snprintf(sts, sizeof(sts),
            "RPiLinearServo v%d.%d.%d\r\n"
            "Config: ERROR\r\n%s\r\n",
            FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH,
            err_buf);
        msc_disk_set_status(sts);
    }

    // Regenerate CONFIG.INI from (possibly updated) g_config
    msc_disk_refresh();
    return ok;
}

// ── TinyUSB MSC callbacks ──────────────────────────────────────────────

extern "C" void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                                    uint8_t product_id[16],
                                    uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id,   "RPiLnSrv", 8);
    memcpy(product_id,  "Config Drive    ", 16);
    memcpy(product_rev, "0.1 ", 4);
}

extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    return true;
}

extern "C" void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                                     uint16_t *block_size) {
    (void)lun;
    *block_count = DISK_BLOCK_NUM;
    *block_size  = DISK_BLOCK_SIZE;
}

extern "C" bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                                       bool start, bool load_eject) {
    (void)lun; (void)power_condition; (void)start; (void)load_eject;
    return true;
}

extern "C" int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba,
                                      uint32_t offset, void *buffer,
                                      uint32_t bufsize) {
    (void)lun;
    if (lba >= DISK_BLOCK_NUM) return -1;

    const uint8_t *addr = s_disk[lba] + offset;
    memcpy(buffer, addr, bufsize);
    return (int32_t)bufsize;
}

extern "C" bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return true;
}

extern "C" int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba,
                                       uint32_t offset, uint8_t *buffer,
                                       uint32_t bufsize) {
    (void)lun;
    if (lba >= DISK_BLOCK_NUM) return -1;

    uint8_t *addr = s_disk[lba] + offset;
    memcpy(addr, buffer, bufsize);

    // Track writes to CONFIG.INI data blocks
    if (lba >= BLK_CFG && lba <= BLK_CFG_END) {
        s_cfg_dirty = true;
        s_last_write_time = get_absolute_time();
    }

    // Also detect root directory writes (host may update file size / name)
    if (lba == BLK_ROOT) {
        s_cfg_dirty = true;
        s_last_write_time = get_absolute_time();
    }

    return (int32_t)bufsize;
}

extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_cmd[16],
                                    void *buffer, uint16_t bufsize) {
    (void)lun; (void)buffer; (void)bufsize;

    switch (scsi_cmd[0]) {
    default:
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }
}

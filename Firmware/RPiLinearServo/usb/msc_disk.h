#pragma once

// ── msc_disk.h ──────────────────────────────────────────────────────────
// Virtual FAT12 disk for USB Mass Storage.
// Exposes CONFIG.INI (R/W) and STATUS.TXT (RO) on a small RAM-backed volume.

#ifdef __cplusplus
extern "C" {
#endif

/// Initialise the virtual disk (generate FAT image from current config).
void msc_disk_init(void);

/// Call from the main loop to detect write-idle and trigger config apply.
/// Returns true if config was just applied.
bool msc_disk_poll(void);

/// Regenerate CONFIG.INI content from current g_config.
void msc_disk_refresh(void);

/// Set the STATUS.TXT content (e.g. after config apply result).
void msc_disk_set_status(const char *text);

#ifdef __cplusplus
}
#endif

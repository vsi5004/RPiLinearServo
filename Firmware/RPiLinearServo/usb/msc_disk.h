#pragma once

// Virtual FAT12 disk for USB Mass Storage.
// Exposes CONFIG.INI (R/W) on a small RAM-backed volume.

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

#ifdef __cplusplus
}
#endif

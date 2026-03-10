#pragma once

// ── config_store.h ──────────────────────────────────────────────────────
// Binary config persistence in flash for user-editable settings.
// Separate from NVM (homing/position) — uses its own 4 KB sector.

#include "config.h"

/// Load config from flash into cfg.
/// @return true if valid config was loaded; false means defaults are used.
bool config_load(ServoConfig &cfg);

/// Save cfg to flash (erase + program).
/// @return true on success.
bool config_save(const ServoConfig &cfg);

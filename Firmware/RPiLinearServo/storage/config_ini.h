#pragma once

#include "config.h"
#include <cstddef>

/// Parse an INI text buffer and apply values to cfg.
/// @param ini_text  Null-terminated INI text.
/// @param len       Length of text.
/// @param cfg       Config struct to modify (in-place).
/// @param err_buf   Buffer for error message (set on failure).
/// @param err_size  Size of err_buf.
/// @return true if all values parsed successfully.
bool config_ini_parse(const char *ini_text, size_t len,
                      ServoConfig &cfg, char *err_buf, size_t err_size);

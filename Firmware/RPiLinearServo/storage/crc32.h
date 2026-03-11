#pragma once

// CRC32 (IEEE 802.3 polynomial, bit-by-bit — no lookup table).
// Used by position_store, config_store, and hall calibration persistence.

#include <cstdint>
#include <cstddef>

inline uint32_t crc32(const void *buf, size_t len) {
    const uint8_t *p = static_cast<const uint8_t *>(buf);
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

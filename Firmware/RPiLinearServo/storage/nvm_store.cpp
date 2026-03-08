// ── nvm_store.cpp ───────────────────────────────────────────────────────
// Stub implementation — interface defined, flash writes deferred to
// a later stage when persistence can be properly tested.

#include "nvm_store.h"
#include <cstdio>
#include <cstring>

bool nvm_load(NvmData &out) {
    printf("[nvm] load: not implemented — returning defaults\n");
    std::memset(&out, 0, sizeof(out));
    out.magic          = NVM_MAGIC;
    out.version        = NVM_VERSION;
    out.homed          = false;
    out.position_steps = 0;
    out.config_crc     = 0;
    out.data_crc       = 0;
    return false;   // no valid data loaded
}

bool nvm_save(const NvmData &data) {
    (void)data;
    printf("[nvm] save: not implemented — skipped\n");
    return false;
}

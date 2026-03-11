// DRV5055 linear hall-effect sensor driver via RP2040 ADC0 (GP26).

#include "hall_sensor.h"
#include "config.h"
#include "pins.h"
#include "flash_map.h"
#include "crc32.h"

#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <cstdio>
#include <cstring>

static bool     s_enabled  = false;
static float    s_filtered = 0.0f;
static bool     s_primed   = false;   // first sample seeds the filter

// GP26 = ADC input 0
static constexpr uint ADC_INPUT = 0;

// 64 back-to-back FIFO samples (~128 µs @ 500 ksps).
// Oversampling by 64 gives 8× noise reduction (√64).
static constexpr uint ADC_OVERSAMPLE = 64;

// Cached integer values from last update() for display use
static uint16_t s_last_raw = 0;
static uint16_t s_last_mv  = 0;

// Calibration LUT
static HallCalEntry s_cal_table[HALL_CAL_MAX_ENTRIES];
static int          s_cal_count = 0;
static bool         s_cal_valid = false;

// Internal: burst-capture N samples via FIFO, return raw sum
static uint32_t hall_burst_sum() {
    adc_select_input(ADC_INPUT);
    adc_fifo_setup(true, false, 0, false, false);
    adc_fifo_drain();
    adc_run(true);
    uint32_t sum = 0;
    for (uint i = 0; i < ADC_OVERSAMPLE; i++)
        sum += adc_fifo_get_blocking();
    adc_run(false);
    adc_fifo_drain();
    return sum;
}

void hall_sensor_init() {
    if (!g_config.use_hall_effect) return;

    adc_init();
    adc_gpio_init(PIN_HALL);    // disable digital I/O on GP26
    adc_select_input(ADC_INPUT);

    s_enabled = true;
    s_primed  = false;
}

bool hall_sensor_enabled() {
    return s_enabled;
}

uint16_t hall_sensor_read_raw() {
    return s_last_raw;
}

uint16_t hall_sensor_read_mv() {
    return s_last_mv;
}

float hall_sensor_filtered_mv() {
    return s_filtered;
}

void hall_sensor_update() {
    if (!s_enabled) return;

    uint32_t sum = hall_burst_sum();

    // Cache integer values for display
    s_last_raw = (uint16_t)(sum / ADC_OVERSAMPLE);
    s_last_mv  = (uint16_t)((uint32_t)s_last_raw * 3300 / 4095);

    // Sub-mV precision float for the filter
    float mv = (float)sum * (3300.0f / (4095.0f * ADC_OVERSAMPLE));

    if (!s_primed) {
        s_filtered = mv;
        s_primed   = true;
    } else {
        float a = g_config.adc_filter_alpha;
        s_filtered = a * mv + (1.0f - a) * s_filtered;
    }
}

// Calibration LUT implementation

bool hall_cal_is_valid() { return s_cal_valid; }
int  hall_cal_count()    { return s_cal_count; }
const HallCalEntry *hall_cal_table() { return s_cal_table; }

void hall_cal_clear() {
    s_cal_count = 0;
    s_cal_valid = false;
}

bool hall_cal_add(int32_t step_pos, float mv) {
    if (s_cal_count >= HALL_CAL_MAX_ENTRIES) return false;
    s_cal_table[s_cal_count++] = {step_pos, mv};
    return true;
}

void hall_cal_set_valid() {
    s_cal_valid = (s_cal_count >= 2);
}

float hall_cal_expected_mv(int32_t step_pos) {
    if (!s_cal_valid || s_cal_count < 2) return 0.0f;

    // Clamp to table range
    if (step_pos <= s_cal_table[0].step_pos)
        return s_cal_table[0].mv;
    if (step_pos >= s_cal_table[s_cal_count - 1].step_pos)
        return s_cal_table[s_cal_count - 1].mv;

    // Binary search for the bracketing interval
    int lo = 0, hi = s_cal_count - 1;
    while (lo + 1 < hi) {
        int mid = (lo + hi) / 2;
        if (s_cal_table[mid].step_pos <= step_pos)
            lo = mid;
        else
            hi = mid;
    }

    // Linear interpolation
    float t = (float)(step_pos - s_cal_table[lo].step_pos)
            / (float)(s_cal_table[hi].step_pos - s_cal_table[lo].step_pos);
    return s_cal_table[lo].mv + t * (s_cal_table[hi].mv - s_cal_table[lo].mv);
}

int32_t hall_cal_estimate_pos(float mv, int32_t hint_pos) {
    if (!s_cal_valid || s_cal_count < 2) return hint_pos;

    // Scan all intervals — the V-curve is non-monotonic, so mV may
    // match in two separate intervals.  Collect all candidates and
    // return the one closest to hint_pos.
    int32_t best_pos  = hint_pos;
    int32_t best_dist = INT32_MAX;

    // Edge clamp: if mV is beyond the first or last entry, include
    // those endpoints as candidates.  Without this, a reading slightly
    // outside the table range finds no match near the edge and snaps
    // to a false match on the other side of the V-curve.
    auto try_candidate = [&](int32_t candidate) {
        int32_t dist = candidate - hint_pos;
        if (dist < 0) dist = -dist;
        if (dist < best_dist) {
            best_dist = dist;
            best_pos  = candidate;
        }
    };

    // Clamp to first entry
    {
        float mv0 = s_cal_table[0].mv;
        float mv1 = s_cal_table[1].mv;
        // If mV is beyond the first entry (on the outer side)
        if ((mv1 < mv0 && mv >= mv0) || (mv1 > mv0 && mv <= mv0)) {
            try_candidate(s_cal_table[0].step_pos);
        }
    }
    // Clamp to last entry
    {
        int last = s_cal_count - 1;
        float mvN  = s_cal_table[last].mv;
        float mvN1 = s_cal_table[last - 1].mv;
        if ((mvN1 < mvN && mv >= mvN) || (mvN1 > mvN && mv <= mvN)) {
            try_candidate(s_cal_table[last].step_pos);
        }
    }

    for (int i = 0; i < s_cal_count - 1; i++) {
        float mv_a = s_cal_table[i].mv;
        float mv_b = s_cal_table[i + 1].mv;
        float lo_mv = mv_a < mv_b ? mv_a : mv_b;
        float hi_mv = mv_a > mv_b ? mv_a : mv_b;

        // Does mv fall within this interval?
        if (mv < lo_mv || mv > hi_mv) continue;

        // Avoid division by zero on flat segments
        float span = mv_b - mv_a;
        if (span > -0.01f && span < 0.01f) continue;

        float t = (mv - mv_a) / span;
        int32_t pos_a = s_cal_table[i].step_pos;
        int32_t pos_b = s_cal_table[i + 1].step_pos;
        int32_t candidate = pos_a + (int32_t)(t * (float)(pos_b - pos_a));

        try_candidate(candidate);
    }

    return best_pos;
}

// Flash persistence for calibration LUT

static constexpr uint32_t CAL_MAGIC         = 0x48434C43;  // "HCLC"
static constexpr uint32_t CAL_VERSION       = 1;

struct __attribute__((packed)) CalFlash {
    uint32_t     magic;
    uint32_t     version;
    uint8_t      count;
    HallCalEntry entries[HALL_CAL_MAX_ENTRIES];
    uint32_t     crc;
};

bool hall_cal_save() {
    CalFlash cf;
    cf.magic   = CAL_MAGIC;
    cf.version = CAL_VERSION;
    cf.count   = static_cast<uint8_t>(s_cal_count);
    std::memcpy(cf.entries, s_cal_table, sizeof(s_cal_table));
    cf.crc     = crc32(&cf, offsetof(CalFlash, crc));

    // Page-aligned buffer (must be multiple of FLASH_PAGE_SIZE = 256)
    static constexpr size_t BUF_SIZE =
        ((sizeof(CalFlash) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
    uint8_t buf[BUF_SIZE];
    std::memset(buf, 0xFF, sizeof(buf));
    std::memcpy(buf, &cf, sizeof(cf));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_OFF_HALL_CAL, FLASH_SECTOR_SZ);
    flash_range_program(FLASH_OFF_HALL_CAL, buf, sizeof(buf));
    restore_interrupts(ints);

    printf("[hall_cal] saved %d entries to flash\n", s_cal_count);
    return true;
}

bool hall_cal_load() {
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(
        FLASH_XIP_BASE + FLASH_OFF_HALL_CAL);

    CalFlash cf;
    std::memcpy(&cf, ptr, sizeof(cf));

    if (cf.magic != CAL_MAGIC || cf.version != CAL_VERSION)
        return false;

    uint32_t expected = crc32(&cf, offsetof(CalFlash, crc));
    if (cf.crc != expected)
        return false;

    if (cf.count > HALL_CAL_MAX_ENTRIES)
        return false;

    s_cal_count = cf.count;
    std::memcpy(s_cal_table, cf.entries, sizeof(s_cal_table));
    s_cal_valid = (s_cal_count >= 2);

    printf("[hall_cal] loaded %d entries from flash\n", s_cal_count);
    return true;
}

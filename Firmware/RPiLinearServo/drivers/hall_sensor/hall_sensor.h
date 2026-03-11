#pragma once

#include <cstdint>

/// Initialise ADC for hall effect sensor (GP26 / ADC0).
/// No-op if use_hall_effect is false.
void hall_sensor_init();

/// True if the sensor is configured and active.
bool hall_sensor_enabled();

/// Raw 12-bit ADC reading (0–4095).
uint16_t hall_sensor_read_raw();

/// Voltage in millivolts (0–3300).
uint16_t hall_sensor_read_mv();

/// EMA-filtered voltage in mV (updated each call to hall_sensor_update).
float hall_sensor_filtered_mv();

/// Sample ADC and update the EMA filter.  Call at your chosen rate (e.g. 250 Hz).
void hall_sensor_update();

/// Maximum number of entries in the calibration lookup table.
static constexpr int HALL_CAL_MAX_ENTRIES = 128;

struct HallCalEntry {
    int32_t  step_pos;   // step position at this sample
    float    mv;         // filtered mV reading
};

/// True if a calibration sweep has been completed.
bool hall_cal_is_valid();

/// Number of entries in the current calibration table.
int hall_cal_count();

/// Direct access to the calibration table (read-only).
const HallCalEntry *hall_cal_table();

/// Clear the calibration table.
void hall_cal_clear();

/// Add an entry to the calibration table.
/// Returns false if the table is full.
bool hall_cal_add(int32_t step_pos, float mv);

/// Mark calibration as complete.
void hall_cal_set_valid();

/// Look up the expected mV for a given step position by linear
/// interpolation of the calibration table.  Returns 0 if not calibrated.
float hall_cal_expected_mv(int32_t step_pos);

/// Inverse lookup: estimate step position from a filtered mV reading.
/// Scans all calibration intervals for matches (handles non-monotonic
/// V-curve) and returns the candidate closest to hint_pos.
/// Returns hint_pos unchanged if not calibrated.
int32_t hall_cal_estimate_pos(float mv, int32_t hint_pos);

/// Save calibration table to flash (see flash_map.h for sector address).
/// @return true on success.
bool hall_cal_save();

/// Load calibration table from flash.
/// @return true if valid data was loaded.
bool hall_cal_load();

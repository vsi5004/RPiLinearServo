// Hardstop homing: drives into the mechanical end-stop for longer than
// the full stroke (home_margin × stroke), then backs off and zeros.
// When the hall sensor is enabled, stall is detected early by monitoring
// the voltage derivative (ΔmV → 0 when the motor stops moving).

#include "homing.h"
#include "stepgen.h"
#include "config.h"
#include "pins.h"
#include "status_led.h"
#include "position_store.h"
#include "hall_sensor.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cmath>

bool g_homed = false;bool g_stall_fault = false;
static constexpr float  STALL_THRESH_MV    = 0.8f;   // |Δfiltered| below this = no motion
static constexpr int    STALL_COUNT_NEEDED = 5;      // consecutive samples (~20 ms at 250 Hz)
static constexpr float  MIN_TRAVEL_FRAC    = 0.20f;   // ignore stall until 20% of home_steps done
static constexpr float  STALL_MIN_RANGE_MV = 15.0f;   // require this much peak-to-peak before trusting stall

bool homing_run() {
    printf("[homing] hardstop homing (margin=%.0f%%)\n",
           g_config.home_margin * 100.0f);
    status_led_set(LedStatus::HOMING);

    gpio_put(PIN_EN, EN_ENABLE);
    sleep_ms(50);


    int32_t  home_steps = static_cast<int32_t>(
                              g_config.stroke_mm * g_config.home_margin
                              * g_config.steps_per_mm());
    uint32_t home_hz    = g_config.home_speed_hz();
    uint32_t accel       = g_config.accel_hz_per_s();

    int32_t signed_steps = g_config.home_dir_negative ? -home_steps : home_steps;

    printf("[homing] driving %ld steps @ %lu Hz toward hardstop\n",
           (long)home_steps, (unsigned long)home_hz);

    stepgen_move_accel(signed_steps, home_hz, accel);

    bool hall_active = hall_sensor_enabled();
    int  stall_count = 0;
    float prev_filt  = 0.0f;
    float hall_min   = 1e9f;   // track range seen during homing
    float hall_max   = -1e9f;
    int32_t min_steps_for_stall = static_cast<int32_t>(home_steps * MIN_TRAVEL_FRAC);
    absolute_time_t next_sample = get_absolute_time();

    if (hall_active) {
        // Prime the filter with a few readings
        for (int i = 0; i < 5; i++) {
            hall_sensor_update();
            sleep_ms(4);
        }
        prev_filt = hall_sensor_filtered_mv();
        printf("[homing] hall stall detection active (threshold=%.1f mV, count=%d, min_range=%.0f mV)\n",
               (double)STALL_THRESH_MV, STALL_COUNT_NEEDED, (double)STALL_MIN_RANGE_MV);
    }

    int home_log_div = 0;          // print every 16th sample (~64 ms)
    while (stepgen_is_busy()) {
        status_led_update();

        if (hall_active
            && absolute_time_diff_us(next_sample, get_absolute_time()) >= 0) {
            hall_sensor_update();
            float cur_filt = hall_sensor_filtered_mv();
            float delta    = fabsf(cur_filt - prev_filt);
            prev_filt      = cur_filt;
            next_sample    = make_timeout_time_ms(4);  // 250 Hz

            // Track observed hall range
            if (cur_filt < hall_min) hall_min = cur_filt;
            if (cur_filt > hall_max) hall_max = cur_filt;
            float hall_range = hall_max - hall_min;

            if ((home_log_div++ & 15) == 0) {
                printf("  home  pos=%ld  raw=%u  filt=%.1f  Δ=%.2f  range=%.1f\n",
                       (long)stepgen_get_position(),
                       hall_sensor_read_mv(),
                       (double)cur_filt, (double)delta,
                       (double)hall_range);
            }

            // Only check stall after minimum travel AND enough hall
            // variation to prove we're in the active sensing region.
            //
            // Exception: if we've traveled more than a full stroke with
            // near-zero hall variation, the motor MUST be stalled — a real
            // stroke traversal would always cross the active sensing zone.
            int32_t traveled = abs(stepgen_get_position());
            int32_t stroke_steps = g_config.stroke_steps();
            bool in_active_zone = (hall_range >= STALL_MIN_RANGE_MV);
            bool flat_beyond_stroke = (!in_active_zone
                                       && traveled >= stroke_steps);

            if (traveled >= min_steps_for_stall
                && (in_active_zone || flat_beyond_stroke)) {
                if (delta < STALL_THRESH_MV) {
                    stall_count++;
                    if (stall_count >= STALL_COUNT_NEEDED) {
                        printf("[homing] hall stall detected at step %ld "
                               "(Δ < %.1f mV × %d)  adc=%u mV  filt=%.1f  "
                               "range=%.1f%s\n",
                               (long)stepgen_get_position(),
                               (double)STALL_THRESH_MV, STALL_COUNT_NEEDED,
                               hall_sensor_read_mv(),
                               (double)cur_filt,
                               (double)hall_range,
                               flat_beyond_stroke
                                   ? "  (flat>stroke — forced)"
                                   : "");
                        stepgen_stop();
                        break;
                    }
                } else {
                    stall_count = 0;
                }
            }
        } else if (!hall_active) {
            sleep_ms(10);
        }
    }

    if (hall_active && (hall_max - hall_min) < STALL_MIN_RANGE_MV) {
        printf("[homing] WARNING: hall range %.1f mV too small — stall detection "
               "was not trusted (flat sensing region). Ran full distance.\n",
               (double)(hall_max - hall_min));
    }

    printf("[homing] stall phase complete, counter = %ld\n",
           (long)stepgen_get_position());

    int32_t backoff        = g_config.backoff_steps();
    int32_t backoff_signed = g_config.home_dir_negative ? backoff : -backoff;
    printf("[homing] backing off %ld steps\n", (long)backoff);

    stepgen_move_accel(backoff_signed, home_hz, accel);
    while (stepgen_is_busy()) {
        status_led_update();
        sleep_ms(10);
    }

    stepgen_reset_position();
    g_homed = true;
    printf("[homing] homed OK — position reset to 0\n");


    if (hall_sensor_enabled()) {
        // Let motor and filter settle after stall + backoff
        printf("[homing] settling before calibration...\n");
        for (int i = 0; i < 50; i++) {   // 200 ms, 50 × 4 ms
            hall_sensor_update();
            sleep_ms(4);
        }

        printf("[homing] starting hall calibration sweep...\n");
        hall_cal_clear();

        int32_t  stroke = g_config.stroke_steps();
        uint32_t cal_hz = g_config.home_speed_hz() / 4;  // ¼ homing speed for accuracy
        if (cal_hz < 500) cal_hz = 500;
        int32_t  sample_interval = stroke / (HALL_CAL_MAX_ENTRIES - 2);
        if (sample_interval < 1) sample_interval = 1;

        // Record starting point
        hall_sensor_update();
        hall_cal_add(0, hall_sensor_filtered_mv());

        // Traverse 0 → stroke
        stepgen_move_accel(stroke, cal_hz, accel);
        int32_t next_sample_pos = sample_interval;

        while (stepgen_is_busy()) {
            status_led_update();
            hall_sensor_update();

            int32_t pos = stepgen_get_position();
            if (pos >= next_sample_pos) {
                float mv = hall_sensor_filtered_mv();
                hall_cal_add(pos, mv);
                printf("  cal  pos=%ld  raw=%u  filt=%.1f\n",
                       (long)pos, hall_sensor_read_mv(), (double)mv);
                next_sample_pos += sample_interval;
            }
            sleep_ms(1);
        }

        // Final sample at end of stroke
        hall_sensor_update();
        float final_mv = hall_sensor_filtered_mv();
        hall_cal_add(stepgen_get_position(), final_mv);
        printf("  cal  pos=%ld  raw=%u  filt=%.1f (final)\n",
               (long)stepgen_get_position(), hall_sensor_read_mv(),
               (double)final_mv);
        hall_cal_set_valid();

        // Measure hall reading at end-of-stroke with motor enabled (holding),
        // then disabled, to quantify stator field / ground shift.
        float mv_motor_on = 0.0f, mv_motor_off = 0.0f;
        {
            // Motor is enabled and holding at end of stroke
            for (int i = 0; i < 25; i++) { hall_sensor_update(); sleep_ms(4); }
            mv_motor_on = hall_sensor_filtered_mv();

            // Disable motor
            gpio_put(PIN_EN, EN_DISABLE);
            for (int i = 0; i < 50; i++) { hall_sensor_update(); sleep_ms(4); }
            mv_motor_off = hall_sensor_filtered_mv();

            // Re-enable motor for return trip
            gpio_put(PIN_EN, EN_ENABLE);
            sleep_ms(50);
            for (int i = 0; i < 25; i++) { hall_sensor_update(); sleep_ms(4); }
            float mv_re_enabled = hall_sensor_filtered_mv();

            printf("[diag] end-of-stroke hall offset test:\n");
            printf("  motor ON  (holding) : %.1f mV\n", (double)mv_motor_on);
            printf("  motor OFF (disabled): %.1f mV\n", (double)mv_motor_off);
            printf("  motor ON  (re-ena)  : %.1f mV\n", (double)mv_re_enabled);
            printf("  offset ON→OFF       : %+.1f mV\n",
                   (double)(mv_motor_off - mv_motor_on));
        }

        printf("[homing] hall calibration: %d points over %ld steps (speed=%lu Hz)\n",
               hall_cal_count(), (long)stroke, (unsigned long)cal_hz);

        // Print a few points for verification
        const HallCalEntry *tbl = hall_cal_table();
        int n = hall_cal_count();
        for (int i = 0; i < n; i += n / 8) {
            printf("  [%3d] step=%ld  mv=%.1f\n",
                   i, (long)tbl[i].step_pos, (double)tbl[i].mv);
        }
        printf("  [%3d] step=%ld  mv=%.1f\n",
               n - 1, (long)tbl[n - 1].step_pos, (double)tbl[n - 1].mv);

        // Return to position 0
        printf("[homing] returning to home position...\n");
        stepgen_move_accel(-stepgen_get_position(), cal_hz, accel);
        while (stepgen_is_busy()) {
            status_led_update();
            sleep_ms(10);
        }

        // Settle and verify actual position via hall sensor.
        // The return trip may have lost steps, so we use the calibration
        // table we just built to find where we really are.
        for (int i = 0; i < 50; i++) {   // 200 ms settle
            hall_sensor_update();
            sleep_ms(4);
        }
        float return_mv = hall_sensor_filtered_mv();
        int32_t actual_pos = hall_cal_estimate_pos(return_mv,
                                                    stepgen_get_position());
        int32_t pos_error = actual_pos - stepgen_get_position();
        printf("[homing] return verify: filt=%.1f mV  counter=%ld  "
               "hall_pos=%ld  error=%ld steps\n",
               (double)return_mv, (long)stepgen_get_position(),
               (long)actual_pos, (long)pos_error);

        // Correct position to match reality, then reset to 0
        // (i.e. true zero = actual_pos steps from here)
        stepgen_set_position(actual_pos);

        // If we're not close to 0, drive back to actual 0
        if (abs(actual_pos) > 10) {
            printf("[homing] correcting: driving %ld steps to reach 0\n",
                   (long)(-actual_pos));
            stepgen_move_accel(-actual_pos, cal_hz, accel);
            while (stepgen_is_busy()) {
                status_led_update();
                sleep_ms(10);
            }
        }
        stepgen_reset_position();
        printf("[homing] calibration complete, back at 0\n");

        {
            for (int i = 0; i < 25; i++) { hall_sensor_update(); sleep_ms(4); }
            float mv_on = hall_sensor_filtered_mv();

            gpio_put(PIN_EN, EN_DISABLE);
            for (int i = 0; i < 50; i++) { hall_sensor_update(); sleep_ms(4); }
            float mv_off = hall_sensor_filtered_mv();

            gpio_put(PIN_EN, EN_ENABLE);
            sleep_ms(50);
            for (int i = 0; i < 25; i++) { hall_sensor_update(); sleep_ms(4); }
            float mv_re = hall_sensor_filtered_mv();

            printf("[diag] home-position hall offset test:\n");
            printf("  motor ON  (holding) : %.1f mV\n", (double)mv_on);
            printf("  motor OFF (disabled): %.1f mV\n", (double)mv_off);
            printf("  motor ON  (re-ena)  : %.1f mV\n", (double)mv_re);
            printf("  offset ON→OFF       : %+.1f mV\n",
                   (double)(mv_off - mv_on));
        }

        // Persist calibration to flash
        hall_cal_save();
    }

    status_led_set(LedStatus::HOMING_DONE);
    // Persist homed state immediately
    PositionState d;
    d.homed          = true;
    d.position_steps = 0;
    position_save(d);
    return true;
}

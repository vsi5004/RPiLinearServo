#include "hall_verify.h"
#include "hall_sensor.h"
#include "homing.h"
#include "config.h"
#include "pins.h"
#include "stepgen.h"
#include "status_led.h"
#include "pwm_input.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

static constexpr float CORRECTION_OK_MV = 10.0f;

void hall_verify_post_move() {
    if (!hall_sensor_enabled() || !hall_cal_is_valid() || g_stall_fault)
        return;

    // Settle briefly — motor just stopped
    for (int i = 0; i < 10; i++) {
        hall_sensor_update();
        sleep_ms(2);
    }

    float   filt     = hall_sensor_filtered_mv();
    int32_t pos      = stepgen_get_position();
    float   expected = hall_cal_expected_mv(pos);
    float   dev      = fabsf(filt - expected);

    if (dev <= g_config.lost_step_threshold_mv)
        return;  // position is fine

    printf("[verify] post-move deviation: pos=%ld  "
           "exp=%.1f  act=%.1f  dev=%.1f mV\n",
           (long)pos, (double)expected, (double)filt, (double)dev);

    // Estimate real position from hall reading
    hall_sensor_update();
    filt = hall_sensor_filtered_mv();
    int32_t est   = hall_cal_estimate_pos(filt, pos);
    int32_t error = pos - est;

    // Reject impossibly large corrections (V-curve ambiguity)
    int32_t max_corr = g_config.stroke_steps() / 4;
    if (abs(error) > max_corr) {
        printf("[fault] estimated error %ld exceeds "
               "max correction %ld — bad estimate?\n",
               (long)error, (long)max_corr);
        status_led_set(LedStatus::STALL_FAULT);
        g_stall_fault = true;
        return;
    }

    if (abs(error) <= 10) {
        printf("[verify] within tolerance (error=%ld steps)\n", (long)error);
        return;
    }

    printf("[verify] correcting: hall_pos=%ld  counter=%ld  error=%ld steps\n",
           (long)est, (long)pos, (long)error);

    // Update counter to actual position, then drive the missing steps
    stepgen_set_position(est);
    gpio_put(PIN_EN, EN_ENABLE);
    stepgen_move_accel(error, g_config.default_speed_hz(),
                       g_config.accel_hz_per_s());
    while (stepgen_is_busy()) {
        pwm_input_poll();
        status_led_update();
        sleep_ms(1);
    }

    // Settle and verify correction
    for (int i = 0; i < 10; i++) {
        hall_sensor_update();
        sleep_ms(2);
    }
    filt     = hall_sensor_filtered_mv();
    pos      = stepgen_get_position();
    expected = hall_cal_expected_mv(pos);
    dev      = fabsf(filt - expected);

    if (dev <= CORRECTION_OK_MV) {
        printf("[verify] correction OK: dev=%.1f mV\n", (double)dev);
    } else {
        printf("[fault] correction failed (dev=%.1f mV) — obstacle?\n",
               (double)dev);
        status_led_set(LedStatus::STALL_FAULT);
        g_stall_fault = true;
    }
}

void hall_log_sample() {
    if (!hall_sensor_enabled())
        return;

    hall_sensor_update();
    int32_t pos  = stepgen_get_position();
    float   mm   = (float)pos / g_config.steps_per_mm();
    float   filt = hall_sensor_filtered_mv();

    if (hall_cal_is_valid()) {
        float expected  = hall_cal_expected_mv(pos);
        float deviation = fabsf(filt - expected);

        printf("[hall] pos=%ld (%.2f mm)  adc=%u (%u mV)  "
               "filt=%.1f  exp=%.1f  dev=%.1f mV\n",
               (long)pos, (double)mm,
               hall_sensor_read_raw(), hall_sensor_read_mv(),
               (double)filt, (double)expected, (double)deviation);
    } else {
        printf("[hall] pos=%ld (%.2f mm)  adc=%u (%u mV)  filt=%.1f mV\n",
               (long)pos, (double)mm,
               hall_sensor_read_raw(), hall_sensor_read_mv(),
               (double)filt);
    }
}

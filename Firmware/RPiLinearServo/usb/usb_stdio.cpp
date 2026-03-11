// CDC-to-stdio adapter for the composite USB device.
// Replaces pico_stdio_usb by registering our own stdio driver that talks
// directly to TinyUSB's CDC class.

#include "usb_stdio.h"
#include "tusb.h"

#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/mutex.h"
#include "pico/time.h"
#include "hardware/irq.h"

static constexpr uint32_t STDIO_USB_STDOUT_TIMEOUT_US   = 500000;
static constexpr uint32_t STDIO_USB_TASK_INTERVAL_US     = 1000;
static constexpr uint32_t STDIO_USB_DEADLOCK_TIMEOUT_MS  = 1000;

static mutex_t s_mutex;
static uint8_t s_low_priority_irq;
static bool    s_usb_stdio_initialised = false;
static alarm_id_t s_task_alarm_id;


static int64_t timer_task(alarm_id_t, void *) {
    if (irq_is_enabled(s_low_priority_irq))
        irq_set_pending(s_low_priority_irq);
    return STDIO_USB_TASK_INTERVAL_US;  // repeat
}

static void low_priority_worker(void) {
    if (mutex_try_enter(&s_mutex, nullptr)) {
        tud_task();
        mutex_exit(&s_mutex);
    }
}

static void cdc_out_chars(const char *buf, int length) {
    static uint64_t last_avail_time;
    if (!mutex_try_enter_block_until(&s_mutex,
            make_timeout_time_ms(STDIO_USB_DEADLOCK_TIMEOUT_MS)))
        return;

    if (tud_cdc_connected()) {
        for (int i = 0; i < length;) {
            int avail = (int)tud_cdc_write_available();
            int n = length - i;
            if (n > avail) n = avail;
            if (n) {
                int n2 = (int)tud_cdc_write(buf + i, (uint32_t)n);
                tud_task();
                tud_cdc_write_flush();
                i += n2;
                last_avail_time = time_us_64();
            } else {
                tud_task();
                tud_cdc_write_flush();
                if (!tud_cdc_connected() ||
                    (!tud_cdc_write_available() &&
                     time_us_64() > last_avail_time + STDIO_USB_STDOUT_TIMEOUT_US))
                    break;
            }
        }
    } else {
        last_avail_time = 0;
    }
    mutex_exit(&s_mutex);
}

static void cdc_out_flush(void) {
    if (!mutex_try_enter_block_until(&s_mutex,
            make_timeout_time_ms(STDIO_USB_DEADLOCK_TIMEOUT_MS)))
        return;
    do { tud_task(); } while (tud_cdc_write_flush());
    mutex_exit(&s_mutex);
}

static int cdc_in_chars(char *buf, int length) {
    int rc = PICO_ERROR_NO_DATA;
    if (tud_cdc_connected() && tud_cdc_available()) {
        if (!mutex_try_enter_block_until(&s_mutex,
                make_timeout_time_ms(STDIO_USB_DEADLOCK_TIMEOUT_MS)))
            return PICO_ERROR_NO_DATA;
        if (tud_cdc_connected() && tud_cdc_available()) {
            int count = (int)tud_cdc_read(buf, (uint32_t)length);
            rc = count ? count : PICO_ERROR_NO_DATA;
        } else {
            tud_task();
        }
        mutex_exit(&s_mutex);
    }
    return rc;
}

static stdio_driver_t stdio_usb_driver = {
    .out_chars = cdc_out_chars,
    .out_flush = cdc_out_flush,
    .in_chars  = cdc_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

// Public API
void usb_stdio_init(void) {
    // Tear down resources that cannot be registered twice (dormant wake re-init).
    if (s_usb_stdio_initialised) {
        cancel_alarm(s_task_alarm_id);
        irq_set_enabled(s_low_priority_irq, false);
        irq_remove_handler(s_low_priority_irq, low_priority_worker);
        user_irq_unclaim(s_low_priority_irq);
    }

    tusb_init();
    mutex_init(&s_mutex);

    s_low_priority_irq = (uint8_t)user_irq_claim_unused(true);
    irq_set_exclusive_handler(s_low_priority_irq, low_priority_worker);
    irq_set_enabled(s_low_priority_irq, true);

    s_task_alarm_id = add_alarm_in_us(STDIO_USB_TASK_INTERVAL_US, timer_task, nullptr, true);

    stdio_set_driver_enabled(&stdio_usb_driver, true);
    s_usb_stdio_initialised = true;
}

bool usb_stdio_connected(void) {
    return tud_cdc_connected();
}

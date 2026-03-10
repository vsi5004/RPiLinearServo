// ── usb_descriptors.cpp ─────────────────────────────────────────────────
// CDC + MSC composite USB descriptors for RPiLinearServo.
// CDC provides the serial CLI, MSC exposes the CONFIG.INI virtual drive.

#include "tusb.h"
#include "pico/unique_id.h"
#include <cstring>

// ── VID / PID ──────────────────────────────────────────────────────────
#define USB_VID   0x2E8A   // Raspberry Pi
#define USB_PID   0x4003   // Custom composite CDC+MSC
#define USB_BCD   0x0200

// ── Interface numbering ────────────────────────────────────────────────
enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MSC,
    ITF_NUM_TOTAL
};

// ── Endpoint addresses ─────────────────────────────────────────────────
#define EPNUM_CDC_NOTIF  0x81
#define EPNUM_CDC_OUT    0x02
#define EPNUM_CDC_IN     0x82
#define EPNUM_MSC_OUT    0x03
#define EPNUM_MSC_IN     0x83

// ── Device descriptor ──────────────────────────────────────────────────
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

// ── Configuration descriptor ───────────────────────────────────────────
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 5, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

// ── String descriptors ─────────────────────────────────────────────────
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC,
    STRID_MSC,
};

static char serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

static const char *const string_desc_arr[] = {
    [STRID_LANGID]       = (const char[]){0x09, 0x04},  // English
    [STRID_MANUFACTURER] = "RPiLinearServo",
    [STRID_PRODUCT]      = "Linear Servo",
    [STRID_SERIAL]       = serial_str,
    [STRID_CDC]          = "Serial CLI",
    [STRID_MSC]          = "Config Drive",
};

// ── TinyUSB callbacks ──────────────────────────────────────────────────

extern "C" const uint8_t *tud_descriptor_device_cb(void) {
    return reinterpret_cast<const uint8_t *>(&desc_device);
}

extern "C" const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

extern "C" const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc_str[32 + 1];

    if (!serial_str[0])
        pico_get_unique_board_id_string(serial_str, sizeof(serial_str));

    uint8_t len;
    if (index == 0) {
        desc_str[1] = 0x0409;
        len = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
            return nullptr;
        const char *str = string_desc_arr[index];
        for (len = 0; len < 31 && str[len]; ++len)
            desc_str[1 + len] = str[len];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc_str;
}

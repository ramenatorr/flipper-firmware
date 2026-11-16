/* USB descriptors: a composite CDC (serial commands) + MSC (SD card in drive mode) device. */

#if !DEBUG_USB_UART

#include "pico/unique_id.h"
#include "tusb.h"

#define USB_VID 0x2E8A
#define USB_PID 0x000C

enum { ITF_CDC = 0, ITF_CDC_DATA, ITF_MSC, ITF_TOTAL };

#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT 0x02
#define EP_CDC_IN 0x82
#define EP_MSC_OUT 0x03
#define EP_MSC_IN 0x83

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static uint8_t const desc_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_MSC, 5, EP_MSC_OUT, EP_MSC_IN, 64),
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_config;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc[32];
    static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    static char const *const strings[] = {
        NULL, "FlipperMCE", "GC Memory Card", serial, "Commands", "SD card",
    };

    if (index == 0) {
        desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
        desc[1] = 0x0409;
        return desc;
    }
    if (index >= sizeof(strings) / sizeof(strings[0]))
        return NULL;
    if (!serial[0])
        pico_get_unique_board_id_string(serial, sizeof(serial));

    char const *str = strings[index];
    uint8_t len = 0;
    while (len < 31 && str[len]) {
        desc[1 + len] = (uint8_t)str[len];
        ++len;
    }
    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc;
}

#endif

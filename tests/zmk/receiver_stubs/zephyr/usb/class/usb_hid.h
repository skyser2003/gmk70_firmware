#pragma once
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
struct usb_setup_packet {
    uint16_t wValue;
};
struct hid_ops {
    int (*get_report)(const struct device *, struct usb_setup_packet *, int32_t *, uint8_t **);
    void (*int_in_ready)(const struct device *);
    void (*protocol_change)(const struct device *, uint8_t);
    void (*on_idle)(const struct device *, uint16_t);
    int (*set_report)(const struct device *, struct usb_setup_packet *, int32_t *, uint8_t **);
    void (*int_out_ready)(const struct device *);
};
void usb_hid_register_device(const struct device *, const uint8_t *, size_t, const struct hid_ops *);
int  usb_hid_init(const struct device *);
int  hid_int_ep_read(const struct device *, uint8_t *, uint32_t, uint32_t *);
int  hid_int_ep_write(const struct device *, const uint8_t *, uint32_t, uint32_t *);

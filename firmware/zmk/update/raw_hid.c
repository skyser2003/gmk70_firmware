/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "raw_hid.h"
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/class/usb_hid.h>

static const uint8_t descriptor[] = {
    0x06, 0x60, 0xff,       /* Usage page FF60 */
    0x09, 0x61,             /* Usage 61 */
    0xa1, 0x01,             /* Application collection */
    0x15, 0x00,             /* Logical minimum 0 */
    0x26, 0xff, 0x00,       /* Logical maximum 255 */
    0x75, 0x08,             /* Report size 8 bits */
    0x95, 0x20,             /* Report count 32, no report ID */
    0x09, 0x61, 0x81, 0x02, /* Input: data, variable, absolute */
    0x09, 0x61, 0x91, 0x02, /* Output: data, variable, absolute */
    0xc0,
};
static const struct device *hid;
K_MSGQ_DEFINE(requests, 32, 4, 4);
static uint8_t response[32];
static bool    response_pending;

static void out_ready(const struct device *dev) {
    uint8_t  request[32];
    uint32_t length = 0;
    if (hid_int_ep_read(dev, request, sizeof(request), &length) == 0 && length == sizeof(request)) {
        /* Bounded drop on overflow: the host's normal timeout/retry applies.
         * No IRQ/USB callback takes the serial mutex or processes flash. */
        (void)k_msgq_put(&requests, request, K_NO_WAIT);
    }
}

static int set_report(const struct device *dev, struct usb_setup_packet *setup, int32_t *length, uint8_t **data) {
    ARG_UNUSED(dev);
    if (sys_le16_to_cpu(setup->wValue) != 0x0200 || *length != 32) return -EINVAL;
    return k_msgq_put(&requests, *data, K_NO_WAIT);
}

static const struct hid_ops ops = {.int_out_ready = out_ready, .set_report = set_report};

int gmk_zmk_raw_hid_init(void) {
    hid = device_get_binding("HID_1");
    if (!hid) return -ENODEV;
    usb_hid_register_device(hid, descriptor, sizeof(descriptor), &ops);
    return usb_hid_init(hid);
}

void gmk_zmk_raw_hid_poll(const gmk_zmk_update_env_t *env) {
    if (!response_pending) {
        uint8_t request[32];
        if (k_msgq_get(&requests, request, K_NO_WAIT) != 0) return;
        gmk_zmk_receiver_receive(request, response, true, env);
        response_pending = true;
    }
    int result = hid_int_ep_write(hid, response, sizeof(response), NULL);
    /* Keep exactly this response until an endpoint becomes writable. Device
     * disconnect/error discards it, leaving protocol STATUS/retry recovery. */
    if (result != -EBUSY && result != -EAGAIN) response_pending = false;
}

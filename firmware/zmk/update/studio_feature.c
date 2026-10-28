/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "raw_hid.h"
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zmk/hid.h>

static const uint8_t feature_descriptor[] = {
    0x06, 0x60, 0xff, 0x09, 0x62, 0xa1, 0x01, 0x85, 0x04, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x20, 0x09, 0x62, 0xb1, 0x02, 0xc0,
};
static uint8_t               descriptor[sizeof(zmk_hid_report_desc) + sizeof(feature_descriptor)];
static struct hid_ops        callbacks;
static const struct hid_ops *original;
static const struct device  *hid;
static struct k_spinlock     lock;
static uint8_t               request[32], response[32], ep0_snapshot[33];
static bool                  pending, response_valid;

static int get_report(const struct device *dev, struct usb_setup_packet *setup, int32_t *length, uint8_t **data) {
    if (sys_le16_to_cpu(setup->wValue) != 0x0304) return original->get_report ? original->get_report(dev, setup, length, data) : -ENOTSUP;
    k_spinlock_key_t key = k_spin_lock(&lock);
    ep0_snapshot[0]      = 4;
    if (response_valid)
        memcpy(ep0_snapshot + 1, response, 32);
    else
        memset(ep0_snapshot + 1, 0, 32);
    k_spin_unlock(&lock, key);
    /* EP0 requests are serialized by the USB stack. Worker publication never
     * changes this snapshot while a control IN transfer consumes it. */
    *length = sizeof(ep0_snapshot);
    *data   = ep0_snapshot;
    return 0;
}

static int set_report(const struct device *dev, struct usb_setup_packet *setup, int32_t *length, uint8_t **data) {
    if (sys_le16_to_cpu(setup->wValue) != 0x0304) return original->set_report ? original->set_report(dev, setup, length, data) : -ENOTSUP;
    if (*length != 33 || !*data || (*data)[0] != 4) return -EINVAL;
    k_spinlock_key_t key    = k_spin_lock(&lock);
    int              result = -EBUSY;
    if (!pending) {
        memcpy(request, *data + 1, 32);
        response_valid = false;
        pending        = true;
        result         = 0;
    }
    k_spin_unlock(&lock, key);
    return result;
}

void __real_usb_hid_register_device(const struct device *, const uint8_t *, size_t, const struct hid_ops *);
void __wrap_usb_hid_register_device(const struct device *dev, const uint8_t *desc, size_t size, const struct hid_ops *ops) {
    /* This build has exactly one HID device. Do not register it twice: the
     * legacy Zephyr registration routine appends its device list node. */
    if (dev != hid || !ops || size != sizeof(zmk_hid_report_desc)) return;
    memcpy(descriptor, desc, size);
    memcpy(descriptor + size, feature_descriptor, sizeof(feature_descriptor));
    original             = ops;
    callbacks            = *ops;
    callbacks.get_report = get_report;
    callbacks.set_report = set_report;
    __real_usb_hid_register_device(dev, descriptor, sizeof(descriptor), &callbacks);
}

int gmk_zmk_raw_hid_init(void) {
    hid = device_get_binding("HID_0");
    return hid ? 0 : -ENODEV;
}

void gmk_zmk_raw_hid_poll(const gmk_zmk_update_env_t *env) {
    uint8_t          in[32], out[32];
    k_spinlock_key_t key  = k_spin_lock(&lock);
    bool             work = pending;
    if (work) memcpy(in, request, 32);
    k_spin_unlock(&lock, key);
    if (!work) return;
    gmk_zmk_receiver_receive(in, out, true, env);
    key = k_spin_lock(&lock);
    memcpy(response, out, 32);
    response_valid = true;
    pending        = false;
    k_spin_unlock(&lock, key);
}

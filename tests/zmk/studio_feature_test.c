/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "raw_hid.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/class/usb_hid.h>
unsigned                     receiver_locked;
static struct device         hid;
static const struct hid_ops *ops;
static uint8_t               prefix[] = {0x05, 1, 0x09, 6, 0xa1, 1, 0x85, 1, 0xc0};
static unsigned              registrations, processed, forwarded;
static uint8_t              *snapshot;
void                         __wrap_usb_hid_register_device(const struct device *, const uint8_t *, size_t, const struct hid_ops *);
const struct device         *device_get_binding(const char *name) {
    assert(!strcmp(name, "HID_0"));
    return &hid;
}
void __real_usb_hid_register_device(const struct device *dev, const uint8_t *desc, size_t size, const struct hid_ops *callbacks) {
    assert(dev == &hid && !registrations++ && size > sizeof(prefix));
    assert(!memcmp(desc, prefix, sizeof(prefix)));
    const uint8_t expected[] = {0x06, 0x60, 0xff, 0x09, 0x62, 0xa1, 1, 0x85, 4, 0x15, 0, 0x26, 0xff, 0, 0x75, 8, 0x95, 32, 0x09, 0x62, 0xb1, 2, 0xc0};
    assert(size == sizeof(prefix) + sizeof(expected));
    assert(!memcmp(desc + sizeof(prefix), expected, sizeof(expected)));
    ops = callbacks;
}
static int original(const struct device *dev, struct usb_setup_packet *s, int32_t *len, uint8_t **data) {
    (void)dev;
    (void)s;
    (void)len;
    (void)data;
    ++forwarded;
    return 27;
}
static void in_ready(const struct device *dev) {
    (void)dev;
}
void gmk_zmk_receiver_receive(const uint8_t in[32], uint8_t out[32], bool usb, const gmk_zmk_update_env_t *env) {
    assert(!receiver_locked && usb && env->left);
    ++processed;
    /* The previous EP0 return pointer must remain stable through worker work. */
    if (snapshot) assert(snapshot[0] == 4 && snapshot[1] == 0);
    memcpy(out, in, 32);
    out[5] = 0xa5;
}
int main(void) {
    assert(!gmk_zmk_raw_hid_init() && !registrations);
    struct hid_ops original_ops = {.get_report = original, .set_report = original, .int_in_ready = in_ready};
    __wrap_usb_hid_register_device(&hid, prefix, sizeof(prefix), &original_ops);
    assert(registrations == 1 && ops->int_in_ready == in_ready && !ops->int_out_ready);
    struct usb_setup_packet s         = {.wValue = 0x0304};
    int32_t                 len       = 33;
    uint8_t                 frame[33] = {4, 0x7f, 0x70, 1, 0, 7};
    uint8_t                *data      = frame;
    assert(!ops->get_report(&hid, &s, &len, &data) && len == 33 && data[0] == 4 && !data[1]);
    snapshot = data;
    data     = frame;
    len      = 32;
    assert(ops->set_report(&hid, &s, &len, &data) == -EINVAL);
    len      = 33;
    frame[0] = 3;
    assert(ops->set_report(&hid, &s, &len, &data) == -EINVAL);
    frame[0] = 4;
    assert(!ops->set_report(&hid, &s, &len, &data) && !processed);
    assert(ops->set_report(&hid, &s, &len, &data) == -EBUSY);
    gmk_zmk_update_env_t env = {.left = true};
    gmk_zmk_raw_hid_poll(&env);
    snapshot = NULL;
    assert(processed == 1);
    assert(!ops->get_report(&hid, &s, &len, &data) && data[1] == 0x7f && data[6] == 0xa5);
    data     = frame;
    frame[9] = 0x92;
    assert(!ops->set_report(&hid, &s, &len, &data));
    assert(!ops->get_report(&hid, &s, &len, &data) && data[1] == 0);
    snapshot = data;
    gmk_zmk_raw_hid_poll(&env);
    assert(snapshot[1] == 0);
    snapshot = NULL;
    assert(!ops->get_report(&hid, &s, &len, &data) && data[9] == 0x92);
    s.wValue = 0x0101;
    assert(ops->get_report(&hid, &s, &len, &data) == 27);
    s.wValue = 0x0201;
    assert(ops->set_report(&hid, &s, &len, &data) == 27 && forwarded == 2);
    puts("Studio Feature: registration, callbacks, framing, busy/invalidation and EP0 snapshot passed");
}

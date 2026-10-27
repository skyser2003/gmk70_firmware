/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "raw_hid.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/class/usb_hid.h>
static const struct hid_ops *callbacks;
static struct device         device;
static unsigned              processed, writes;
static int                   write_error;
static uint32_t              input_length = 32;
static uint8_t               input[32], sent[32];
const struct device         *device_get_binding(const char *name) {
    assert(strcmp(name, "HID_1") == 0);
    return &device;
}
void usb_hid_register_device(const struct device *dev, const uint8_t *desc, size_t size, const struct hid_ops *ops) {
    assert(dev == &device);
    assert(size > 20);
    assert(desc[0] == 0x06 && desc[1] == 0x60 && desc[2] == 0xff && desc[3] == 0x09 && desc[4] == 0x61);
    callbacks = ops;
}
int usb_hid_init(const struct device *dev) {
    assert(dev == &device);
    return 0;
}
int hid_int_ep_read(const struct device *dev, uint8_t *data, uint32_t size, uint32_t *length) {
    assert(dev == &device && size == 32);
    memcpy(data, input, 32);
    *length = input_length;
    return 0;
}
int hid_int_ep_write(const struct device *dev, const uint8_t *data, uint32_t size, uint32_t *written) {
    assert(dev == &device && size == 32);
    (void)written;
    ++writes;
    memcpy(sent, data, 32);
    return write_error;
}
void gmk_zmk_receiver_receive(const uint8_t in[32], uint8_t out[32], bool usb, const gmk_zmk_update_env_t *env) {
    assert(usb && env->left);
    ++processed;
    memcpy(out, in, 32);
    out[5] = 0xa5;
}
int main(void) {
    gmk_zmk_update_env_t env = {.left = true};
    assert(gmk_zmk_raw_hid_init() == 0);
    assert(callbacks);
    input[8] = 0x12;
    callbacks->int_out_ready(&device);
    assert(!processed && !writes);
    gmk_zmk_raw_hid_poll(&env);
    assert(processed == 1 && writes == 1 && sent[8] == 0x12 && sent[5] == 0xa5);
    input_length = 31;
    callbacks->int_out_ready(&device);
    gmk_zmk_raw_hid_poll(&env);
    assert(processed == 1);
    input_length = 32;
    input[8]     = 0x13;
    callbacks->int_out_ready(&device);
    write_error = -EBUSY;
    gmk_zmk_raw_hid_poll(&env);
    assert(processed == 2 && sent[8] == 0x13);
    input[8] = 0x14;
    callbacks->int_out_ready(&device);
    gmk_zmk_raw_hid_poll(&env);
    assert(processed == 2 && sent[8] == 0x13);
    write_error = 0;
    gmk_zmk_raw_hid_poll(&env);
    assert(processed == 2);
    gmk_zmk_raw_hid_poll(&env);
    assert(processed == 3 && sent[8] == 0x14);
    struct usb_setup_packet setup = {.wValue = 0x0200};
    int32_t                 len   = 32;
    uint8_t                *data  = input;
    assert(callbacks->set_report(&device, &setup, &len, &data) == 0);
    assert(processed == 3);
    gmk_zmk_raw_hid_poll(&env);
    assert(processed == 4);
    setup.wValue = 0x0300;
    assert(callbacks->set_report(&device, &setup, &len, &data) < 0);
    setup.wValue = 0x0200;
    len          = 31;
    assert(callbacks->set_report(&device, &setup, &len, &data) < 0);
    for (unsigned i = 0; i < 8; ++i)
        callbacks->int_out_ready(&device);
    for (unsigned i = 0; i < 8; ++i)
        gmk_zmk_raw_hid_poll(&env);
    assert(processed == 8);
    puts("ZMK raw HID queue tests passed");
}

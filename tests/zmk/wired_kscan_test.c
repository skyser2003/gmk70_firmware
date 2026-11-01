/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/zmk/drivers/kscan/kscan_gmk70_wired.c"
int64_t fake_ms;
void (*fake_uart_isr)(const void *);
static bool     left = true, connected = true;
static unsigned events, last_row, last_col, polls;
static bool     last_pressed;
static uint16_t local[5], remote[5];
static int      rotation;
static bool     available = true;
bool            gmk_split_available(void) {
    return available;
}
int gmk_wired_encoder_next(void) {
    int direction = rotation;
    rotation      = 0;
    return direction;
}
void gmk_wired_encoder_clear(void) {
    rotation = 0;
}
bool gmk_wired_is_left(void) {
    return left;
}
void gmk_wired_scan(uint16_t rows[5]) {
    memcpy(rows, local, 10);
}
void gmk_wired_poll(void) {
    ++polls;
}
bool gmk_wired_remote_matrix(uint16_t rows[5]) {
    if (connected)
        memcpy(rows, remote, 10);
    else
        memset(rows, 0, 10);
    return connected;
}
static void event(const struct device *dev, uint32_t r, uint32_t c, bool pressed) {
    assert(dev == scanner);
    ++events;
    last_row     = r;
    last_col     = c;
    last_pressed = pressed;
}
int main(void) {
    struct device dev = {.data = &data, .api = &api};
    assert(init(&dev) == 0);
    assert(api.config(&dev, NULL) == -EINVAL);
    assert(api.config(&dev, event) == 0);
    assert(api.enable_callback(&dev) == 0);
    local[0] = 1;
    scan_once();
    assert(events == 1 && last_row == 0 && last_col == 0 && last_pressed && polls == 1);
    scan_once();
    assert(events == 1);
    remote[4] = 1U << 8;
    scan_once();
    assert(events == 2 && last_row == 9 && last_col == 8 && last_pressed);
    connected = false;
    scan_once();
    assert(events == 3 && last_row == 9 && !last_pressed);
    assert(api.disable_callback(&dev) == 0);
    local[0] = 0;
    scan_once();
    assert(events == 3);
    assert(api.enable_callback(&dev) == 0);
    scan_once();
    assert(events == 4 && last_row == 0 && !last_pressed);
    left            = false;
    local[4]        = 1U << 8;
    unsigned before = polls;
    scan_once();
    assert(events == 5 && last_row == 9 && last_col == 8 && last_pressed && polls == before);
    left = true;
    memset(local, 0, sizeof(local));
    memset(remote, 0, sizeof(remote));
    scan_once();
    unsigned at = events;
    rotation    = 1;
    scan_once();
    assert(events == at + 1 && last_row == 10 && last_col == 1 && last_pressed);
    fake_ms += 4;
    scan_once();
    assert(events == at + 1);
    fake_ms += 1;
    scan_once();
    assert(events == at + 2 && !last_pressed);
    rotation = -1;
    scan_once();
    assert(events == at + 2);
    fake_ms += 5;
    scan_once();
    assert(events == at + 3 && last_col == 0 && last_pressed);
    assert(api.disable_callback(&dev) == 0);
    scan_once();
    assert(events == at + 4 && !last_pressed);
    rotation = 1;
    scan_once();
    assert(!rotation && events == at + 4);
    assert(api.enable_callback(&dev) == 0);
    fake_ms += 5;
    left     = false;
    rotation = 1;
    scan_once();
    assert(!rotation && events == at + 4);
    left      = true;
    available = false;
    rotation  = 1;
    scan_once();
    assert(!rotation && events == at + 4);
    available = true;
    fake_ms   = UINT32_MAX - 2;
    rotation  = 1;
    scan_once();
    assert(last_pressed && events == at + 5);
    fake_ms += 5;
    scan_once();
    assert(!last_pressed && events == at + 6);
    fake_ms += 5;
    rotation = 1;
    scan_once();
    assert(last_pressed && events == at + 7);
    available = false;
    scan_once();
    assert(!last_pressed && events == at + 8);
    puts("native kscan physical/remote/encoder pulse/disable/mode/wraparound tests passed");
}

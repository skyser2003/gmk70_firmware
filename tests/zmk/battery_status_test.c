/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "battery_status.h"
#include "image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t request[32] = {0x7f, 0x72, 1, 0, 0x21, 0, 0, 0, 0x12, 0x34, 0x56, 0x78};

static void packet_validation(void) {
    uint8_t changed[32];
    assert(gmk_battery_status_packet(request));
    for (unsigned i = 0; i < 8; ++i) {
        memcpy(changed, request, sizeof(changed));
        changed[i] ^= 1;
        assert(!gmk_battery_status_packet(changed));
    }
    for (unsigned i = 12; i < 32; ++i) {
        memcpy(changed, request, sizeof(changed));
        changed[i] = 1;
        assert(!gmk_battery_status_packet(changed));
    }
    memcpy(changed, request, sizeof(changed));
    memset(changed + 8, 0, 4);
    assert(!gmk_battery_status_packet(changed));
    changed[8] = 1;
    assert(gmk_battery_status_packet(changed));
    memset(changed + 8, 0xff, 4);
    assert(gmk_battery_status_packet(changed));
}

static void reply_layout_and_bounds(void) {
    struct {
        uint8_t before;
        uint8_t data[32];
        uint8_t after;
    } response                                   = {.before = 0x5a, .after = 0xa5};
    const struct gmk_battery_status status       = {.age_ms = 10000, .queries = 0x04030201, .replies = 0x08070605, .errors = 0xabcdef12, .percentage = 0, .valid = true, .cable = true, .full = true};
    const uint8_t                   expected[32] = {0x7f, 0x72, 1, 0, 0x21, 0, 0, 3, 0x12, 0x34, 0x56, 0x78, 0x42, 0x41, 0x54, 0x54, 0, 7, 0, 0, 0x10, 0x27, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    memset(response.data, 0xff, sizeof(response.data));
    gmk_battery_status_reply(request, &status, response.data);
    assert(response.before == 0x5a && response.after == 0xa5);
    assert(!memcmp(response.data, expected, sizeof(expected)));
}

static void freshness_percentage_and_gpio_flags(void) {
    uint8_t                   response[32];
    struct gmk_battery_status status = {.age_ms = 10000, .percentage = 100, .valid = true};
    gmk_battery_status_reply(request, &status, response);
    assert(response[16] == 100 && response[17] == 1);
    status.age_ms = 10001;
    status.cable = status.full = true;
    gmk_battery_status_reply(request, &status, response);
    assert(response[16] == 255 && response[17] == 6);
    assert(gmk_read_u32(response + 20) == 10001);
    status.age_ms = UINT32_MAX;
    gmk_battery_status_reply(request, &status, response);
    assert(response[16] == 255 && response[17] == 6);
    assert(gmk_read_u32(response + 20) == UINT32_MAX);
    status.age_ms = 0;
    status.valid  = false;
    gmk_battery_status_reply(request, &status, response);
    assert(response[16] == 255 && response[17] == 6);
    status.valid      = true;
    status.percentage = 101;
    gmk_battery_status_reply(request, &status, response);
    assert(response[16] == 255 && response[17] == 6);
    status.percentage = 255;
    gmk_battery_status_reply(request, &status, response);
    assert(response[16] == 255 && response[17] == 6);
    status.percentage = 50;
    status.cable      = false;
    gmk_battery_status_reply(request, &status, response);
    assert(response[16] == 50 && response[17] == 1);
}

int main(void) {
    packet_validation();
    reply_layout_and_bounds();
    freshness_percentage_and_gpio_flags();
    puts("Battery status: strict read-only packet, bounded layout, freshness and GPIO flags passed");
    return 0;
}

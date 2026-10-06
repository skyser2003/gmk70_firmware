// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "via.h"
#include "stock_v15_fixture.h"

static unsigned commands, replies;
static uint8_t  reply[32];
void            via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length) {
    assert(length == 32 && data[0] == 7 && data[1] == 3 && data[2] == 2 && data[3] == 44);
    ++commands;
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(length == 32);
    memcpy(reply, data, length);
    ++replies;
}
int main(void) {
    // V15's effect setter clamps every out-of-range value, retaining the echo.
    for (unsigned effect = 0; effect < 256; ++effect) {
        uint8_t packet[32] = {7, 3, 2, effect};
        assert(via_command_kb(packet, 32) == (effect > 44));
        assert(packet[3] == effect);
    }
    assert(commands == 211 && replies == 211);
    for (unsigned value = 0; value < 256; ++value) {
        uint8_t packet[32] = {7, 3, 1, value};
        assert(via_command_kb(packet, 32));
        assert(stock_v15_fixture.brightness == value);
        packet[0] = 8;
        packet[3] = 0;
        assert(via_command_kb(packet, 32) && packet[3] == value);
    }
    uint8_t packet[32] = {7, 0, 1, 6};
    assert(via_command_kb(packet, 32) && stock_v15_fixture.sleep == 6);
    packet[0] = 8;
    packet[3] = 0;
    assert(via_command_kb(packet, 32) && packet[3] == 6);
    memset(packet, 0, 32);
    packet[0] = 0x21;
    packet[1] = 1;
    packet[2] = 1;
    assert(via_command_kb(packet, 32) && stock_v15_fixture.nkro == 1);
    packet[1] = 0;
    assert(via_command_kb(packet, 32) && packet[1] == 1);
    memset(packet, 0, 32);
    packet[0] = 1;
    assert(via_command_kb(packet, 32) && reply[1] == 0 && reply[2] == 12);
    packet[0] = 0x0A;
    assert(via_command_kb(packet, 32) && stock_v15_fixture.reset == 1);
    const unsigned activity = stock_v15_fixture.activity;
    for (uint8_t length = 0; length < 32; ++length)
        assert(!via_command_kb(packet, length));
    assert(stock_v15_fixture.activity == activity);
    uint8_t read_effect[32] = {8, 3, 2};
    assert(!via_command_kb(read_effect, 32));
    puts("Stock VIA protocol, brightness routing, timeout selector, NKRO and effect bounds passed");
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "via.h"

static unsigned commands;
static unsigned replies;

void via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length) {
    assert(length == 32 && data[0] == 7 && data[1] == 3 && data[2] == 2 && data[3] == 0);
    ++commands;
}

void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(length == 32 && data[3] == 45);
    ++replies;
}

int main(void) {
    uint8_t close_all[32] = {7, 3, 2, 45};
    assert(via_command_kb(close_all, sizeof(close_all)));
    assert(commands == 1 && replies == 1 && close_all[3] == 45);
    // All ordinary effects, brightness, and reads must reach upstream handling.
    for (uint8_t effect = 0; effect < 45; ++effect) {
        uint8_t packet[32] = {7, 3, 2, effect};
        assert(!via_command_kb(packet, sizeof(packet)) && packet[3] == effect);
    }
    uint8_t brightness[32]  = {7, 3, 1, 255};
    uint8_t read_effect[32] = {8, 3, 2, 45};
    assert(!via_command_kb(brightness, sizeof(brightness)) && brightness[3] == 255);
    assert(!via_command_kb(read_effect, sizeof(read_effect)));
    for (uint8_t length = 0; length < 4; ++length) {
        assert(!via_command_kb(close_all, length));
    }
    assert(commands == 1 && replies == 1);
    puts("Original VIA lighting command routing passed");
}

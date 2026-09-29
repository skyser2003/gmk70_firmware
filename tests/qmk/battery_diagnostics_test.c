// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "battery_diagnostics.h"
#include "stock_keycodes.h"
#include "via.h"

static uint32_t                now = 0x12345678;
static unsigned                replies;
static unsigned                snapshots;
static battery_diag_snapshot_t current = {
    .layers         = 2,
    .default_layers = 1,
    .flags          = BATTERY_DIAG_MASTER | BATTERY_DIAG_RGB_ENABLED,
    .board_device   = 6,
    .radio_device   = 6,
    .battery        = 80,
    .peer_battery   = 50,
    .rgb_mode       = 13,
    .brightness     = 32,
    .pins           = 0x7F,
};

uint32_t timer_read32(void) {
    return now;
}

battery_diag_snapshot_t battery_diagnostics_snapshot(void) {
    ++snapshots;
    return current;
}

void via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length) {
    (void)data;
    (void)length;
    assert(false); // Diagnostic requests must never enter the RGB setter.
}

void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(length == 32 && data[0] == 2 && data[1] == 0x80);
    assert(data[3] == 'G' && data[4] == 'B' && data[5] == 1);
    ++replies;
}

static uint32_t get_u32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

static void read_page(uint8_t *data, uint8_t page) {
    memset(data, 0, 32);
    data[0] = 2;
    data[1] = 0x80;
    data[2] = page;
    assert(via_command_kb(data, 32));
}

static void test_getter_boundary(void) {
    // Every other command ID must pass through unchanged, including setters.
    for (unsigned command = 0; command <= UINT8_MAX; ++command) {
        if (command == 2) continue;
        uint8_t packet[32] = {command, 0x80};
        assert(!via_command_kb(packet, sizeof(packet)));
        assert(packet[0] == command && packet[1] == 0x80);
    }
    for (uint8_t length = 0; length < 32; ++length) {
        uint8_t packet[32] = {2, 0x80};
        assert(!via_command_kb(packet, length));
    }
    uint8_t oversized[33]     = {2, 0x80};
    uint8_t bad_page[32]      = {2, 0x80, 8};
    uint8_t normal_getter[32] = {2, 4};
    uint8_t payload[32]       = {2, 0x80, 0, 1};
    assert(!via_command_kb(oversized, sizeof(oversized)));
    assert(!via_command_kb(bad_page, sizeof(bad_page)));
    assert(!via_command_kb(normal_getter, sizeof(normal_getter)));
    assert(!via_command_kb(payload, sizeof(payload)));
    assert(replies == 0 && snapshots == 0);
}

static void test_history_survives_release_and_mode_change(void) {
    uint8_t data[32];
    battery_diagnostics_key(0x0005, true, BATTERY_DIAG_ENTERED); // Ordinary B is not recorded.
    read_page(data, 2);
    assert(get_u32(data + 6) == 0 && snapshots == 0);

    battery_diagnostics_key(HS_BATQ, true, BATTERY_DIAG_ENTERED);
    current.flags |= BATTERY_DIAG_LEVEL_REQUEST;
    battery_diagnostics_key(HS_BATQ, true, BATTERY_DIAG_LEVEL_HANDLED);
    battery_diagnostics_render(BATTERY_DIAG_INDICATOR);
    battery_diagnostics_render(BATTERY_DIAG_LEVEL_DRAW);
    battery_diagnostics_key(HS_BATQ, false, BATTERY_DIAG_ENTERED);
    current.flags &= ~BATTERY_DIAG_LEVEL_REQUEST;
    current.board_device = 0;
    current.radio_device = 0;
    now += 1000;

    read_page(data, 1);
    assert(data[6] == 0x7E && data[7] == 0x1B && data[8] == BATTERY_DIAG_LEVEL_HANDLED);
    assert(get_u32(data + 9) == 0x12345678);
    assert(get_u32(data + 13) == 2 && get_u32(data + 17) == 1);
    assert((data[22] & BATTERY_DIAG_LEVEL_REQUEST) != 0);
    assert(data[23] == 6 && data[24] == 6 && data[26] == 80);
    assert(data[31] == 0x7F); // Last byte of the packed 19-byte snapshot.
    uint8_t saved[32];
    memcpy(saved, data, sizeof(saved));
    read_page(data, 1);
    assert(memcmp(saved, data, sizeof(saved)) == 0); // Reading is non-destructive.

    read_page(data, 0);
    assert(get_u32(data + 6) == now && data[20] == 0 && data[21] == 0);
    assert((data[19] & BATTERY_DIAG_LEVEL_REQUEST) == 0);
    read_page(data, 2);
    assert(get_u32(data + 6) == 1 && get_u32(data + 10) == 1);
    assert(get_u32(data + 14) == 1 && get_u32(data + 18) == 1);
    assert(get_u32(data + 22) == 0 && get_u32(data + 26) == 0x12345678);

    battery_diagnostics_key(KC_BATQ, true, BATTERY_DIAG_ENTERED);
    battery_diagnostics_key(KC_BATQ, true, BATTERY_DIAG_RECORDING_CONSUMED);
    read_page(data, 1);
    assert(data[7] == 7 && data[8] == BATTERY_DIAG_RECORDING_CONSUMED);
    battery_diagnostics_key(KC_2G4, true, BATTERY_DIAG_ENTERED);
    battery_diagnostics_key(KC_2G4, true, BATTERY_DIAG_DEVICE_CONSUMED);
    read_page(data, 1);
    assert(data[7] == 5 && data[8] == BATTERY_DIAG_DEVICE_CONSUMED);
    battery_diagnostics_key(KC_BATQ, true, BATTERY_DIAG_ENTERED);
    battery_diagnostics_key(KC_BATQ, true, BATTERY_DIAG_CHARGING_HANDLED);
    battery_diagnostics_render(BATTERY_DIAG_CHARGING_DRAW);
    read_page(data, 1);
    assert(data[8] == BATTERY_DIAG_CHARGING_HANDLED);
    read_page(data, 2);
    assert(get_u32(data + 6) == 4 && get_u32(data + 22) == 1);
}

static void test_final_frame_survives_release(void) {
    uint8_t data[32];
    battery_diagnostics_frame(2, 12, 23, 34, 80, 0, 0);
    uint32_t held_at = now;
    now += 100;
    battery_diagnostics_frame(0, 23, 34, 45, 23, 34, 45);
    read_page(data, 2);
    assert(data[30] == 7);
    read_page(data, 3);
    assert(get_u32(data + 6) == 2 && get_u32(data + 10) == 1);
    assert(get_u32(data + 14) == held_at && data[18] == 2);
    assert(data[19] == 12 && data[20] == 23 && data[21] == 34);
    assert(data[22] == 80 && data[23] == 0 && data[24] == 0);
    assert(data[25] == 23 && data[26] == 34 && data[27] == 45);
    for (uint8_t page = 4; page <= 7; ++page) {
        read_page(data, page);
        for (uint8_t i = 6; i < 32; ++i)
            assert(data[i] == 0);
    }
}

int main(void) {
    test_getter_boundary();
    test_history_survives_release_and_mode_change();
    test_final_frame_survives_release();
    puts("Battery diagnostic getter isolation and retained event history passed");
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stock_battery.h"
#include "transactions.h"

static unsigned sends;
static bool     transport_ok = true;
static uint8_t  last_command;
static uint8_t  pixels[77][3];
static bool     right_half;

bool transaction_rpc_exec(int8_t id, uint8_t request_length, const void *request, uint8_t response_length, void *response) {
    assert(id == USER_SYNC_MMS && request_length == 5 && response_length == 5);
    const uint8_t *bytes = request;
    for (unsigned i = 1; i < 5; ++i)
        assert(bytes[i] == 0);
    last_command = bytes[0];
    ++sends;
    // The local request must change before the attempted send, even on failure.
    if (last_command == 0xA4 || last_command == 0xA5) assert(stock_battery_level_requested() == (last_command == 0xA4));
    if (last_command == 0xF0 || last_command == 0xF1) assert(stock_battery_charging_requested() == (last_command == 0xF0));
    memset(response, 0xFF, response_length); // Stock key handler ignores response.
    return transport_ok;
}

void rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    assert(index >= 0 && index < 77);
    // Match QMK's global LED ownership: left 0..35, right 36..76.
    if ((index >= 36) != right_half) return;
    pixels[index][0] = red;
    pixels[index][1] = green;
    pixels[index][2] = blue;
}

void rgb_matrix_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < 77; ++i)
        rgb_matrix_set_color(i, red, green, blue);
}

static void test_key_events(void) {
    stock_battery_key(0x0004, true, true);
    stock_battery_key(0x7E1B, true, false);
    stock_battery_key(0x7E1B, false, false);
    assert(sends == 0 && !stock_battery_level_requested());
    stock_battery_key(0x7E1B, true, true);
    assert(sends == 1 && last_command == 0xA4);
    stock_battery_key(0x7E07, true, false);
    assert(sends == 2 && last_command == 0xF0);
    stock_battery_key(0x7E1B, false, true);
    assert(sends == 3 && last_command == 0xA5);
    stock_battery_key(0x7E07, false, false);
    assert(sends == 4 && last_command == 0xF1);

    transport_ok = false;
    stock_battery_key(0x7E1B, true, true);
    assert(sends == 5 && stock_battery_level_requested());
    // Rendering a held request adds no refresh/retry transactions.
    for (unsigned i = 0; i < 1000; ++i)
        stock_battery_render(75);
    assert(sends == 5);
    // Preserve the observed stock guard even if mode changes before release.
    stock_battery_key(0x7E1B, false, false);
    assert(sends == 5 && stock_battery_level_requested());
    stock_battery_key(0x7E1B, false, true);
    assert(sends == 6 && !stock_battery_level_requested());
    transport_ok = true;
}

static void test_receiver_and_rendering(void) {
    const unsigned before   = sends;
    uint8_t        response = 0xCC;
    assert(stock_battery_receive(0xA4, &response) && response == 0);
    assert(stock_battery_level_requested());
    const uint8_t levels[] = {0, 9, 10, 29, 30, 50, 59, 60, 90, 100};
    const uint8_t bar[]    = {5, 6, 7, 8, 9, 39, 40, 41, 42, 43};
    for (unsigned p = 0; p < sizeof(levels); ++p) {
        for (unsigned half = 0; half < 2; ++half) {
            right_half = half;
            memset(pixels, 0x55, sizeof(pixels));
            stock_battery_render(levels[p]);
            for (unsigned led = 0; led < 77; ++led) {
                uint8_t expected[3] = {0, 0, 0};
                if ((led >= 36) != right_half) {
                    memset(expected, 0x55, sizeof(expected));
                } else {
                    for (unsigned segment = 0; segment < 10; ++segment) {
                        if (led == bar[segment] && (segment == 0 || segment < levels[p] / 10)) expected[levels[p] < 30 ? 0 : 1] = 150;
                    }
                }
                assert(memcmp(pixels[led], expected, 3) == 0);
            }
        }
    }
    assert(stock_battery_receive(0xF0, &response) && response == 0);
    assert(stock_battery_charging_requested() && stock_battery_level_requested());
    assert(stock_battery_receive(0xF1, &response) && response == 1);
    assert(!stock_battery_charging_requested() && stock_battery_level_requested());
    assert(stock_battery_receive(0xA5, &response) && response == 0);
    memset(pixels, 0x55, sizeof(pixels));
    stock_battery_render(100);
    for (unsigned i = 0; i < sizeof(pixels); ++i)
        assert(((uint8_t *)pixels)[i] == 0x55);
    for (unsigned command = 0; command <= UINT8_MAX; ++command) {
        if (command == 0xA4 || command == 0xA5 || command == 0xF0 || command == 0xF1) continue;
        response = 0xCC;
        assert(!stock_battery_receive(command, &response) && response == 0xCC);
    }
    assert(sends == before); // Receiving or rendering must never echo commands.
}

int main(void) {
    test_key_events();
    test_receiver_and_rendering();
    puts("Stock battery: event packets, USB guard, failed-send behavior and both halves' shared bar passed");
}

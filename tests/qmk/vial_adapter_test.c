// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "eeprom.h"
#include "stock_v15_fixture.h"
#include "wireless.h"

static bool     native_enabled;
static uint32_t marker;
static unsigned writes, forwarded, replies;
static uint8_t  device = DEVS_BT1;
uint8_t         wireless_get_current_devs(void) {
    return device;
}
bool __real_nvm_eeconfig_is_enabled(void) {
    return native_enabled;
}
void __real_nvm_eeconfig_enable(void) {
    native_enabled = true;
}
uint32_t eeprom_read_dword(const uint32_t *address) {
    assert((uintptr_t)address == 0xFFC);
    return marker;
}
void eeprom_update_dword(uint32_t *address, uint32_t value) {
    assert((uintptr_t)address == 0xFFC);
    marker = value;
    ++writes;
}
void __real_raw_hid_receive(uint8_t *data, uint8_t length) {
    assert(length == 32);
    ++forwarded;
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(length == 32);
    ++replies;
}
bool __wrap_nvm_eeconfig_is_enabled(void);
void __wrap_nvm_eeconfig_enable(void);
void __wrap_raw_hid_receive(uint8_t *data, uint8_t length);

int main(void) {
    // A valid stock core word cannot validate a native Vial storage layout.
    native_enabled                   = true;
    const uint32_t invalid_markers[] = {0, 0xFFFFFFFF, 0x19082600, 0x564D4700, 0x564D4701};
    for (unsigned i = 0; i < sizeof(invalid_markers) / sizeof(invalid_markers[0]); ++i) {
        marker = invalid_markers[i];
        assert(!__wrap_nvm_eeconfig_is_enabled() && writes == 0);
    }
    __wrap_nvm_eeconfig_enable();
    assert(__wrap_nvm_eeconfig_is_enabled() && writes == 1);
    assert(marker == 0x564D4702);
    native_enabled = false;
    assert(!__wrap_nvm_eeconfig_is_enabled());
    // Identification, bootloader, reset and every Vial unlock operation must
    // go through the fork's real handler and its permission checks.
    for (unsigned command = 0; command < 256; ++command) {
        uint8_t  data[32] = {command, 0xA5, 0xA5};
        unsigned before   = forwarded;
        __wrap_raw_hid_receive(data, 32);
        assert(forwarded == before + (command != 0x21));
    }
    for (unsigned command = 0; command <= 13; ++command) {
        uint8_t  data[32] = {0xFE, command};
        unsigned before   = forwarded;
        __wrap_raw_hid_receive(data, 32);
        assert(forwarded == before + 1);
    }
    for (unsigned selector = 0; selector < 8; ++selector) {
        uint8_t  data[32] = {7, 0, 1, selector};
        unsigned before   = forwarded;
        __wrap_raw_hid_receive(data, 32);
        assert(stock_v15_fixture.sleep == selector && forwarded == before);
        data[0] = 8;
        data[3] = 0xFF;
        __wrap_raw_hid_receive(data, 32);
        assert(data[3] == selector && forwarded == before);
    }
    uint8_t data[32] = {0x21, 1, 1};
    __wrap_raw_hid_receive(data, 32);
    data[1] = 0;
    __wrap_raw_hid_receive(data, 32);
    assert(data[1] == 1);
    const unsigned activity = stock_v15_fixture.activity, before = forwarded;
    for (unsigned length = 0; length < 32; ++length)
        __wrap_raw_hid_receive(data, length);
    assert(stock_v15_fixture.activity == activity && forwarded == before);
    assert(replies == 19 && writes == 1);

    puts("PASS: Vial schema isolation, native protocol/unlock routing and packet bounds");
    return 0;
}

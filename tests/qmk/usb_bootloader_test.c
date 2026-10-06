// SPDX-License-Identifier: GPL-2.0-or-later
// Stock's ordinary command requests a later board reset in both distributions.
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "via.h"
#include "wireless.h"
#include "md_raw.h"
#include "stock_v15_fixture.h"
void               md_receive_raw_cb(uint8_t *data, uint8_t length);
static transport_t transport;
static unsigned    usb_replies, radio_replies, forwarded;
bool               is_keyboard_master(void) {
    return true;
}
transport_t get_transport(void) {
    return transport;
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(length == 32 && data[0] == 0x0B && data[1] == 1);
    assert(stock_v15_fixture.boot == usb_replies + radio_replies + 1);
    ++usb_replies;
}
void md_send_raw(uint8_t *data, uint8_t length) {
    assert(length == 32 && data[0] == 0x0B && data[1] == 1);
    assert(stock_v15_fixture.boot == usb_replies + radio_replies + 1);
    ++radio_replies;
}
void raw_hid_receive(uint8_t *data, uint8_t length) {
    ++forwarded;
    assert(via_command_kb(data, length));
}
void via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length) {
    assert(false);
}
void reset_keyboard(void) {
    assert(!"Reset must be deferred until the board task runs");
}
int main(void) {
    uint8_t packet[33] = {0x0B};
    for (uint8_t length = 0; length <= 33; ++length)
        if (length != 32) assert(!via_command_kb(packet, length));
    for (unsigned mode = TRANSPORT_NONE; mode <= TRANSPORT_WLS; ++mode) {
        transport = mode;
        memset(packet, 0x55, sizeof(packet));
        packet[0] = 0x0B;
        assert(via_command_kb(packet, 32) && packet[1] == 1);
        md_receive_raw_cb(packet, 32);
    }
    assert(forwarded == 3 && stock_v15_fixture.boot == 6);
    assert(usb_replies + radio_replies == 6);
    puts("Stock deferred bootloader request: both profiles, echo, padding and radio delivery passed");
}

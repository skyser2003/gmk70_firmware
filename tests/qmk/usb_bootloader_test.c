// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "via.h"
#include "usb_main.h"
#include "wireless.h"
#include "md_raw.h"

void md_receive_raw_cb(uint8_t *data, uint8_t length);

typedef struct {
    bool        master;
    transport_t transport;
    unsigned    usb_replies;
    unsigned    radio_replies;
    unsigned    radio_forwarded;
    unsigned    resets;
    uint16_t    wait;
    uint8_t     response;
} boot_fixture_t;

static boot_fixture_t fixture;
test_usb_driver_t     USB_DRIVER;

bool is_keyboard_master(void) {
    return fixture.master;
}
transport_t get_transport(void) {
    return fixture.transport;
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(length == 32);
    fixture.response = data[0];
    ++fixture.usb_replies;
}
void md_send_raw(uint8_t *data, uint8_t length) {
    assert(length == 32);
    fixture.response = data[0];
    ++fixture.radio_replies;
}
void raw_hid_receive(uint8_t *data, uint8_t length) {
    ++fixture.radio_forwarded;
    // Radio requests must never reach a functioning bootloader handler.
    assert(!via_command_kb(data, length));
}
void via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length) {
    assert(false);
}
void wait_ms(uint16_t milliseconds) {
    assert(fixture.usb_replies == 1 && fixture.response == id_bootloader_jump);
    assert(fixture.resets == 0);
    fixture.wait += milliseconds;
}
void reset_keyboard(void) {
    assert(fixture.usb_replies == 1 && fixture.radio_replies == 0);
    assert(fixture.wait == 100);
    ++fixture.resets;
}

static void reset_fixture(void) {
    fixture          = (boot_fixture_t){.master = true, .transport = TRANSPORT_USB};
    USB_DRIVER.state = USB_ACTIVE;
}

static void test_explicit_command(void) {
    reset_fixture();
    uint8_t packet[32] = {id_bootloader_jump};
#ifdef GMK70_DEBUG_USB_BOOTLOADER
    assert(via_command_kb(packet, sizeof(packet)));
    assert(fixture.resets == 1 && packet[0] == id_bootloader_jump);
#else
    assert(!via_command_kb(packet, sizeof(packet)));
    assert(fixture.resets == 0 && fixture.usb_replies == 0 && fixture.wait == 0);
#endif
}

static void test_packet_boundaries(void) {
    reset_fixture();
    uint8_t packet[33] = {id_bootloader_jump};
    for (uint8_t length = 0; length <= sizeof(packet); ++length) {
        if (length != 32) assert(!via_command_kb(packet, length));
    }
    for (unsigned command = 0; command <= UINT8_MAX; ++command) {
        packet[0] = command;
        if (command != id_bootloader_jump) assert(!via_command_kb(packet, 32));
    }
    assert(fixture.resets == 0 && fixture.usb_replies == 0 && fixture.wait == 0);

    for (uint8_t offset = 1; offset < 32; ++offset) {
        reset_fixture();
        memset(packet, 0, sizeof(packet));
        packet[0]      = id_bootloader_jump;
        packet[offset] = 1;
#ifdef GMK70_DEBUG_USB_BOOTLOADER
        assert(via_command_kb(packet, 32));
        assert(fixture.usb_replies == 1 && fixture.response == id_unhandled);
#else
        assert(!via_command_kb(packet, 32));
#endif
        assert(fixture.resets == 0 && fixture.wait == 0);
    }
}

static void test_connection_gate(void) {
    for (uint8_t invalid = 0; invalid < 4; ++invalid) {
        reset_fixture();
        if (invalid == 0) fixture.master = false;
        if (invalid == 1) fixture.transport = TRANSPORT_WLS;
        if (invalid == 2) fixture.transport = TRANSPORT_NONE;
        if (invalid == 3) USB_DRIVER.state = 0;
        uint8_t packet[32] = {id_bootloader_jump};
#ifdef GMK70_DEBUG_USB_BOOTLOADER
        assert(via_command_kb(packet, sizeof(packet)));
        assert(fixture.response == id_unhandled);
#else
        assert(!via_command_kb(packet, sizeof(packet)));
#endif
        assert(fixture.resets == 0 && fixture.wait == 0);
    }
}

static void test_radio_cannot_request_bootloader(void) {
    const transport_t transports[] = {TRANSPORT_USB, TRANSPORT_WLS, TRANSPORT_NONE};
    for (uint8_t i = 0; i < sizeof(transports) / sizeof(transports[0]); ++i) {
        reset_fixture();
        fixture.transport  = transports[i];
        uint8_t packet[32] = {id_bootloader_jump};
        md_receive_raw_cb(packet, sizeof(packet));
#ifdef GMK70_DEBUG_USB_BOOTLOADER
        assert(fixture.radio_forwarded == 0 && fixture.radio_replies == 1);
        assert(fixture.usb_replies == 0 && fixture.response == id_unhandled);
#else
        assert(fixture.radio_forwarded == 1 && fixture.radio_replies == 0);
#endif
        assert(fixture.resets == 0 && fixture.wait == 0);
        // Unrelated radio commands keep their original dispatch path.
        packet[0]             = 1;
        const unsigned before = fixture.radio_forwarded;
        md_receive_raw_cb(packet, sizeof(packet));
        assert(fixture.radio_forwarded == before + 1);
    }
}

int main(void) {
    test_explicit_command();
    test_packet_boundaries();
    test_connection_gate();
    test_radio_cannot_request_bootloader();
    puts("USB bootloader: build boundary, acknowledgement order, packet validation and radio rejection passed");
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "radio_fixture.h"
#include "raw_hid.h"

void md_receive_raw_cb(uint8_t *data, uint8_t length);

static void test_transport_selection(void) {
    radio_fixture.state = MD_STATE_DISCONNECTED;
    wireless_devs_change(DEVS_USB, DEVS_BT1, false);
    assert(radio_fixture.transport == TRANSPORT_WLS);
    assert(radio_fixture.selected_device == DEVS_BT1);
    wireless_devs_change(DEVS_BT1, DEVS_2G4, true);
    assert(radio_fixture.transport == TRANSPORT_WLS);
    assert(radio_fixture.pairing);
    wireless_devs_change(DEVS_2G4, DEVS_USB, false);
    assert(radio_fixture.transport == TRANSPORT_USB);
    radio_fixture.master = false;
    wireless_devs_change(DEVS_USB, DEVS_BT1, false);
    assert(radio_fixture.transport == TRANSPORT_USB);
    radio_fixture.master = true;
    radio_fixture.state  = MD_STATE_CONNECTED;
}

static bool radio_has_key(uint8_t key) {
    for (uint8_t slot = 2; slot < MD_SND_CMD_KB_LEN; slot++) {
        if (radio_fixture.keyboard[slot] == key) {
            return true;
        }
    }
    return key < MD_SND_CMD_NKRO_LEN * 8 && (radio_fixture.nkro[key / 8] & (1u << (key % 8)));
}

static void test_rollover_and_release(void) {
    report_nkro_t report = {.mods = 2};
    for (uint8_t key = 4; key < 14; key++) {
        report.bits[key / 8] |= 1u << (key % 8);
    }
    wireless_driver.send_nkro(&report);
    for (uint8_t key = 4; key < 14; key++) {
        assert(radio_has_key(key));
    }
    // Release nonadjacent slots and all overflow keys, leaving holes before held keys.
    memset(report.bits, 0, sizeof(report.bits));
    report.bits[5 / 8] |= 1u << (5 % 8);
    report.bits[9 / 8] |= 1u << (9 % 8);
    wireless_driver.send_nkro(&report);
    for (uint8_t key = 1; key < 112; key++) {
        assert(radio_has_key(key) == (key == 5 || key == 9));
    }
    assert(radio_fixture.keyboard[0] == 2);
    wireless_driver.send_nkro(NULL);
    for (uint8_t key = 1; key < 112; key++) {
        assert(!radio_has_key(key));
    }
    // Repeated changing chords exercise release/reassignment and bounds under ASan.
    for (unsigned frame = 0; frame < 500; frame++) {
        memset(report.bits, 0, sizeof(report.bits));
        for (uint8_t key = 4; key < 112; key++) {
            if ((key + frame) % 13 < 2) {
                report.bits[key / 8] |= 1u << (key % 8);
            }
        }
        wireless_driver.send_nkro(&report);
        for (uint8_t key = 4; key < 112; key++) {
            assert(radio_has_key(key) == ((key + frame) % 13 < 2));
        }
    }
    wireless_driver.send_nkro(NULL);
}

static void test_keyboard_report(void) {
    report_keyboard_t report = {.mods = 3, .keys = {4, 5}};
#ifdef KEYBOARD_SHARED_EP
    report.report_id = REPORT_ID_KEYBOARD;
#endif
    wireless_driver.send_keyboard(&report);
    const uint8_t expected[8] = {3, 0, 4, 5, 0, 0, 0, 0};
    assert(memcmp(radio_fixture.keyboard, expected, sizeof(expected)) == 0);
}

static void test_extra_reports(void) {
    report_extra_t report = {.report_id = REPORT_ID_SYSTEM, .usage = 0x82};
    wireless_driver.send_extra(&report);
    assert(radio_fixture.system == 2);
    report.usage = 0;
    wireless_driver.send_extra(&report);
    assert(radio_fixture.system == 0);
    report.report_id = REPORT_ID_CONSUMER;
    report.usage     = 0x81; // Must remain a consumer usage despite matching a system value.
    wireless_driver.send_extra(&report);
    assert(radio_fixture.consumer == 0x81);
    report.usage = 0;
    wireless_driver.send_extra(&report);
    assert(radio_fixture.consumer == 0);
}

static void test_raw_hid_routing(void) {
    uint8_t data[32]        = {0};
    radio_fixture.transport = TRANSPORT_USB;
    raw_hid_send(data, sizeof(data));
    assert(radio_fixture.usb_raw_count == 1 && radio_fixture.radio_raw_count == 0);
    radio_fixture.transport = TRANSPORT_WLS;
    raw_hid_send(data, sizeof(data));
    assert(radio_fixture.radio_raw_count == 1);
    raw_hid_send(data, sizeof(data) - 1);
    assert(radio_fixture.radio_raw_count == 1);
    md_receive_raw_cb(data, sizeof(data));
    md_receive_raw_cb(data, sizeof(data) - 1);
    assert(radio_fixture.receive_raw_count == 1);
}

static void test_mode_sync_wakes_sleeping_peer(void) {
    for (lpwr_state_t state = LPWR_NORMAL; state <= LPWR_WAKEUP; ++state) {
        for (lpwr_prepare_t cls = LPWR_PREPARE_ORDINARY; cls <= LPWR_PREPARE_CRITICAL; ++cls) {
            lpwr_set_state(state);
            lpwr_set_prepare_class(cls);
            radio_fixture.master = false;
            wireless_devs_change(DEVS_BT1, DEVS_BT1, false);
            bool asleep = state == LPWR_PRESLEEP || state == LPWR_STOP;
            assert(lpwr_get_state() == (asleep ? LPWR_WAKEUP : state));
            assert(lpwr_get_prepare_class() == (asleep ? LPWR_PREPARE_ORDINARY : cls));
        }
    }
    lpwr_set_state(LPWR_NORMAL);
    lpwr_set_prepare_class(LPWR_PREPARE_ORDINARY);
    radio_fixture.master = true;
}

int main(void) {
    test_mode_sync_wakes_sleeping_peer();
    test_transport_selection();
    test_rollover_and_release();
    test_keyboard_report();
    test_extra_reports();
    test_raw_hid_routing();
    puts("PASS: transport, rollover/release, keyboard framing, extra reports, raw HID");
    return 0;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Native report/action code and real module RX; only USB/UART hardware is inert.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "quantum.h"
#include "quantum_keycodes.h"
#include "action_util.h"
#include "action_layer.h"
#include "command.h"
#include "debug.h"
#include "function_flip_qmk.h"
#include "keycode_config.h"
#include "stock_keycodes.h"
#include "usb_main.h"
#include "usb_device_state.h"
#include "wireless.h"
#include "wls/wls.h"
#include "smsg.h"

static gmk70_flip_state_t stock_flip;
static struct {
    uint8_t devs, last_btdevs;
} confinfo                = {.devs = DEVS_BT1, .last_btdevs = DEVS_BT1};
static uint32_t       now = 100;
static uint8_t        rx[64], rx_count, rx_cursor;
static uint8_t        radio_keyboard[MD_SND_CMD_KB_LEN], radio_nkro[MD_SND_CMD_NKRO_LEN];
static uint16_t       radio_consumer;
static bool           gpio_bt, gpio_2g4 = true;
static bool           flip_enabled;
static bool           parsing;
static unsigned       report_count;
static lpwr_state_t   power_state;
static lpwr_prepare_t power_class;
keymap_config_t       keymap_config;
debug_config_t        debug_config;
test_usb_driver_t     USB_DRIVER = {.state = USB_ACTIVE};
host_driver_t         chibios_driver;
bool                  last_suspend_state;
extern host_driver_t  wireless_driver;
void                  wireless_task(void);

static bool process_gmk70_record(uint16_t keycode, keyrecord_t *record) {
    return gmk70_flip_process_record(&stock_flip, keycode, record->event.pressed, flip_enabled, true);
}
bool process_rgb_matrix(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    return true;
}
bool is_keyboard_master(void) {
    return true;
}
uint32_t timer_read32(void) {
    return now;
}
uint32_t timer_elapsed32(uint32_t value) {
    return now - value;
}
uint32_t sync_timer_read32(void) {
    return now;
}
uint32_t sync_timer_elapsed32(uint32_t value) {
    return now - value;
}
void uart_init(uint32_t baud) {
    assert(baud == 115200);
}
bool uart_available(void) {
    return rx_cursor < rx_count;
}
uint8_t uart_read(void) {
    assert(rx_cursor < rx_count);
    return rx[rx_cursor++];
}
void uart_transmit(const uint8_t *data, uint32_t length) {
    if (data[0] >= MD_SND_CMD_SEND_KB && data[0] <= MD_SND_CMD_SEND_SYSTEM) {
        assert(!parsing && "Native report recovery must run outside radio parser ownership");
        ++report_count;
    }
    if (data[0] == MD_SND_CMD_SEND_KB) memcpy(radio_keyboard, data + 1, sizeof(radio_keyboard));
    if (data[0] == MD_SND_CMD_SEND_NKRO) memcpy(radio_nkro, data + 1, sizeof(radio_nkro));
    if (data[0] == MD_SND_CMD_SEND_CONSUMER) memcpy(&radio_consumer, data + 1, sizeof(radio_consumer));
    (void)length;
}
void register_mouse(uint8_t code, bool pressed) {
    (void)code;
    (void)pressed;
    assert(false);
}
void                      rgb_matrix_increase_val(void) {}
void                      rgb_matrix_decrease_val(void) {}
static usb_hid_protocol_t usb_protocol = USB_PROTOCOL_REPORT;
usb_hid_protocol_t        usb_device_state_get_protocol(void) {
    return usb_protocol;
}
void usb_device_state_set_protocol(usb_hid_protocol_t value) {
    usb_protocol = value;
}
void usbWakeupHost(test_usb_driver_t *driver) {
    (void)driver;
}
void wait_ms(unsigned ms) {
    (void)ms;
}
void lpwr_set_timeout_manual(bool value) {
    (void)value;
}
void         last_matrix_activity_trigger(void) {}
void         lpwr_task(void) {}
void         lpwr_activity_trigger(void) {}
lpwr_state_t lpwr_get_state(void) {
    return power_state;
}
void lpwr_set_state(lpwr_state_t value) {
    power_state = value;
}
lpwr_prepare_t lpwr_get_prepare_class(void) {
    return power_class;
}
void lpwr_set_prepare_class(lpwr_prepare_t value) {
    power_class = value;
}
void wireless_devs_change_kb(uint8_t old, uint8_t device, bool reset) {
    (void)old;
    (void)reset;
    confinfo.devs = device;
}
bool gpio_read_pin(uint32_t pin) {
    if (pin == HS_BAT_CABLE_PIN) return true;
    assert(pin == C14 || pin == C15);
    return pin == C15 ? gpio_bt : gpio_2g4;
}
void hs_rgb_blink_set_timer(uint32_t value) {
    (void)value;
}
void suspend_wakeup_init(void) {}

#include "radio_lifecycle_fixture.h"

static void receive(const uint8_t *bytes, uint8_t length) {
    assert(length <= sizeof(rx));
    memcpy(rx, bytes, length);
    rx_count  = length;
    rx_cursor = 0;
    parsing   = true;
    md_main_task();
    parsing = false;
}
static void drain(void) {
    const uint8_t ack[] = {0x61, 0x0d, 0x0a};
    for (unsigned attempt = 0; smsg_is_busy(); ++attempt) {
        assert(attempt < 100);
        receive(ack, sizeof(ack));
        md_main_task();
    }
}
static void status(uint8_t code, bool service) {
    const uint8_t bytes[] = {MD_REV_CMD_DEVCTRL, code, (uint8_t)(MD_REV_CMD_DEVCTRL + code)};
    receive(bytes, sizeof(bytes));
    if (service) wireless_task();
}
static void key(uint16_t code, bool pressed) {
    keyrecord_t record = {.event = {.type = KEY_EVENT, .key = {.row = 0, .col = 0}, .pressed = pressed}};
    if (pressed) clear_weak_mods(); // Native process_record_handler clears these before the board hook.
    if (process_record_kb(code, &record)) {
        action_t action = {.code = code};
        process_action(&record, action);
    }
}
static bool radio_has(uint8_t code) {
    for (unsigned slot = 2; slot < sizeof(radio_keyboard); ++slot)
        if (radio_keyboard[slot] == code) return true;
    return code < sizeof(radio_nkro) * 8 && (radio_nkro[code / 8] & (1u << (code % 8)));
}
static void expect_empty(void) {
    for (unsigned slot = 0; slot < sizeof(keyboard_report->keys); ++slot)
        assert(keyboard_report->keys[slot] == 0);
    for (unsigned byte = 0; byte < sizeof(nkro_report->bits); ++byte)
        assert(nkro_report->bits[byte] == 0);
    assert(get_mods() == 0 && get_weak_mods() == 0 && gmk70_stock_flip_modifiers() == 0);
}
static void initialize(bool nkro) {
    keymap_config.nkro = nkro;
    md_init();
    host_set_driver(&chibios_driver);
    wireless_devs_change(DEVS_USB, DEVS_BT1, false);
    drain();
    status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
}
static void lost(uint8_t code) {
    key(KC_A, true);
    assert(radio_has(KC_A));
    status(code, true);
    key(KC_A, false);
    status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
    key(KC_B, true);
    assert(!radio_has(KC_A) && radio_has(KC_B) && "A released while radio unavailable must not return beside B");
}
static void physical_modes(void) {
    const uint8_t destinations[] = {DEVS_USB, DEVS_BT1, DEVS_BT5, DEVS_2G4};
    for (unsigned pins = 0; pins < 4; ++pins) {
        gpio_bt  = (pins & 2) != 0;
        gpio_2g4 = (pins & 1) != 0;
        for (unsigned previous = DEVS_USB; previous <= DEVS_2G4; ++previous) {
            for (unsigned index = 0; index < sizeof(destinations); ++index) {
                confinfo.devs   = previous;
                uint8_t device  = destinations[index];
                bool    allowed = pins != 0;
                assert(selected_device_allowed(device) == allowed && "Physical admission must reject both-low regardless of the old logical device");
            }
        }
    }
}
int main(int argc, char **argv) {
    assert(argc == 3);
    initialize(strcmp(argv[2], "1") == 0);
    if (!strcmp(argv[1], "released"))
        lost(MD_REV_CMD_DEVCTRL_DISCONNECTED);
    else if (!strcmp(argv[1], "pairing"))
        lost(MD_REV_CMD_DEVCTRL_PAIRING);
    else if (!strcmp(argv[1], "pairing-cache")) {
        key(KC_A, true);
        status(MD_REV_CMD_DEVCTRL_PAIRING, true);
        key(KC_A, false);
        status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
        assert(!radio_has(KC_A) && "Reconnect must bypass native deduplication for a release suppressed during pairing");
    } else if (!strcmp(argv[1], "consumer-cache")) {
        flip_enabled = true;
        key(KC_1, true);
        assert(radio_consumer != 0);
        status(MD_REV_CMD_DEVCTRL_PAIRING, true);
        key(KC_1, false);
        status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
        assert(radio_consumer == 0 && "Reconnect must clear consumer state through native host-cache suppression");
    } else if (!strcmp(argv[1], "reject"))
        lost(MD_REV_CMD_DEVCTRL_REJECT);
    else if (!strcmp(argv[1], "held")) {
        key(KC_A, true);
        // The inactive protocol format must not preserve an old bitmap either.
        if (keymap_config.nkro)
            keyboard_report->keys[0] = KC_C;
        else
            nkro_report->bits[KC_C / 8] |= (uint8_t)(1u << (KC_C % 8));
        status(MD_REV_CMD_DEVCTRL_DISCONNECTED, true);
        expect_empty();
        status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
        key(KC_B, true);
        assert(!radio_has(KC_A) && radio_has(KC_B));
        key(KC_A, false);
        assert(radio_has(KC_B));
    } else if (!strcmp(argv[1], "modifiers")) {
        key(KC_LCTL, true);
        key(LGUI(KC_A), true);
        status(MD_REV_CMD_DEVCTRL_DISCONNECTED, false);
        key(KC_B, false); // A main-context input boundary must consume pending loss too.
        expect_empty();
        key(KC_LCTL, false);
        key(LGUI(KC_A), false);
        status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
        key(KC_B, true);
        assert(radio_keyboard[0] == 0 && !radio_has(KC_A));
    } else if (!strcmp(argv[1], "blocked-press")) {
        status(MD_REV_CMD_DEVCTRL_DISCONNECTED, true);
        key(LGUI(KC_A), true);
        key(KC_LCTL, true);
        expect_empty();
    } else if (!strcmp(argv[1], "flip")) {
        flip_enabled = true;
        key(KC_3, true);
        assert(radio_keyboard[0] == 8 && radio_has(KC_TAB));
        status(MD_REV_CMD_DEVCTRL_DISCONNECTED, true);
        expect_empty();
        status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
        key(KC_B, true);
        assert(!radio_has(KC_TAB) && radio_keyboard[0] == 0);
    } else if (!strcmp(argv[1], "switch")) {
        key(KC_A, true);
        wireless_devs_change(DEVS_BT1, DEVS_BT2, false);
        drain();
        status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
        key(KC_B, true);
        assert(!radio_has(KC_A) && radio_has(KC_B));
    } else if (!strcmp(argv[1], "reset")) {
        key(KC_A, true);
        wireless_devs_change(DEVS_BT1, DEVS_BT1, true);
        expect_empty();
        drain();
        status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
        key(KC_B, true);
        assert(!radio_has(KC_A) && radio_has(KC_B));
    } else if (!strcmp(argv[1], "usb-switch")) {
        key(KC_A, true);
        wireless_devs_change(DEVS_BT1, DEVS_USB, false);
        expect_empty();
        assert(!radio_has(KC_A));
        drain();
        key(KC_B, true);
        assert(is_key_pressed(KC_B));
        wireless_devs_change(DEVS_USB, DEVS_BT1, false);
        expect_empty();
        drain();
        status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
        assert(!radio_has(KC_A) && !radio_has(KC_B));
    } else if (!strcmp(argv[1], "repeated")) {
        key(KC_A, true);
        unsigned count = report_count;
        status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
        assert(is_key_pressed(KC_A) && report_count == count);
        status(MD_REV_CMD_DEVCTRL_DISCONNECTED, false);
        status(MD_REV_CMD_DEVCTRL_CONNECTED, true);
        expect_empty();
    } else if (!strcmp(argv[1], "same-channel")) {
        key(KC_A, true);
        wireless_devs_change(DEVS_BT1, DEVS_BT1, false);
        assert(is_key_pressed(KC_A) && radio_has(KC_A));
    } else if (!strcmp(argv[1], "physical-mode"))
        physical_modes();
    else
        assert(false);
    printf("Radio lifecycle %s NKRO=%s passed\n", argv[1], argv[2]);
}

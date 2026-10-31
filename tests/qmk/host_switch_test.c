// SPDX-License-Identifier: GPL-2.0-or-later
// Real transport, wireless encoder and module queue; simulated USB/UART only.
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "usb_main.h"
#include "usb_device_state.h"
#include "wireless.h"
#include "smsg.h"

extern host_driver_t wireless_driver;
typedef enum { EVENT_USB_KEYBOARD, EVENT_USB_NKRO, EVENT_USB_CONSUMER, EVENT_USB_SYSTEM, EVENT_USB_MOUSE, EVENT_USB_DRIVER, EVENT_RADIO_DRIVER, EVENT_RADIO_PACKET } event_kind_t;
typedef struct {
    event_kind_t kind;
    uint8_t      bytes[50];
    uint32_t     length;
} event_t;
static event_t           events[128];
static unsigned          event_count, ack_cursor = 3;
static uint32_t          now    = 1000;
static bool              master = true;
static lpwr_state_t      power_state;
static lpwr_prepare_t    power_class;
static host_driver_t    *driver;
static report_keyboard_t usb_keyboard;
static report_nkro_t     usb_nkro;
static report_extra_t    usb_consumer, usb_system;
static report_mouse_t    usb_mouse;
test_usb_driver_t        USB_DRIVER = {.state = USB_ACTIVE};
bool                     last_suspend_state;

static void event(event_kind_t kind) {
    assert(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count++] = (event_t){.kind = kind};
}
static void usb_send_keyboard(report_keyboard_t *report) {
    assert(report != NULL);
    usb_keyboard = *report;
    event(EVENT_USB_KEYBOARD);
}
static void usb_send_nkro(report_nkro_t *report) {
    assert(report != NULL && report->report_id == REPORT_ID_NKRO);
    usb_nkro = *report;
    event(EVENT_USB_NKRO);
}
static void usb_send_extra(report_extra_t *report) {
    assert(report != NULL);
    if (report->report_id == REPORT_ID_CONSUMER) {
        usb_consumer = *report;
        event(EVENT_USB_CONSUMER);
    } else {
        assert(report->report_id == REPORT_ID_SYSTEM);
        usb_system = *report;
        event(EVENT_USB_SYSTEM);
    }
}
static void usb_send_mouse(report_mouse_t *report) {
    assert(report != NULL);
    usb_mouse = *report;
    event(EVENT_USB_MOUSE);
}
host_driver_t  chibios_driver = {.send_keyboard = usb_send_keyboard, .send_nkro = usb_send_nkro, .send_extra = usb_send_extra, .send_mouse = usb_send_mouse};
host_driver_t *host_get_driver(void) {
    return driver;
}
void host_set_driver(host_driver_t *value) {
    driver = value;
    event(value == &chibios_driver ? EVENT_USB_DRIVER : EVENT_RADIO_DRIVER);
}
bool is_keyboard_master(void) {
    return master;
}
uint32_t sync_timer_read32(void) {
    return now;
}
uint32_t sync_timer_elapsed32(uint32_t stamp) {
    return now - stamp;
}
void uart_init(uint32_t baud) {
    assert(baud == 115200);
}
bool uart_available(void) {
    return ack_cursor < 3;
}
uint8_t uart_read(void) {
    const uint8_t ack[] = {0x61, 0x0d, 0x0a};
    assert(ack_cursor < sizeof(ack));
    return ack[ack_cursor++];
}
void uart_transmit(const uint8_t *data, uint32_t length) {
    event(EVENT_RADIO_PACKET);
    event_t *entry = &events[event_count - 1];
    assert(length <= sizeof(entry->bytes));
    memcpy(entry->bytes, data, length);
    entry->length = length;
}
void usb_device_state_set_protocol(usb_hid_protocol_t value) {
    assert(value == USB_PROTOCOL_REPORT);
}
bool gpio_read_pin(uint32_t pin) {
    assert(pin == HS_BAT_CABLE_PIN);
    return true;
}
void usbWakeupHost(test_usb_driver_t *value) {
    assert(value == &USB_DRIVER);
}
void wait_ms(unsigned ms) {
    (void)ms;
}
void last_matrix_activity_trigger(void) {}
void lpwr_activity_trigger(void) {}
void lpwr_task(void) {}
void lpwr_set_timeout_manual(bool requested) {
    (void)requested;
}
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

static void drain_commands(void) {
    for (unsigned attempts = 0; smsg_is_busy(); ++attempts) {
        assert(attempts < 100);
        md_main_task();
        ack_cursor = 0;
        md_main_task();
        md_main_task();
    }
}
static void reset(uint8_t device) {
    md_init();
    USB_DRIVER.state = USB_STOP;
    master           = true;
    driver           = &chibios_driver;
    wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);
    drain_commands();
    wireless_devs_change(DEVS_USB, device, false);
    drain_commands();
    *md_getp_state() = MD_STATE_CONNECTED;
    USB_DRIVER.state = USB_ACTIVE;
    event_count      = 0;
}
static void assert_radio_zero(unsigned index, uint8_t command, uint32_t length) {
    const event_t *entry = &events[index];
    assert(entry->kind == EVENT_RADIO_PACKET && entry->bytes[0] == command && entry->length == length);
    for (uint32_t i = 1; i + 1 < length; ++i)
        assert(entry->bytes[i] == 0);
    assert(entry->bytes[length - 1] == command);
}
static void assert_radio_releases(void) {
    assert_radio_zero(0, 0xa1, 10);
    assert_radio_zero(1, 0xa1, 10); // NKRO encoder also resets its six-key slots.
    assert_radio_zero(2, 0xa2, 16);
    assert_radio_zero(3, 0xa3, 4);
    assert_radio_zero(4, 0xa4, 3);
    assert_radio_zero(5, 0xa8, 7);
}
static void assert_selector(unsigned index, uint8_t channel) {
    const event_t *entry = &events[index];
    assert(entry->kind == EVENT_RADIO_PACKET && entry->length == 3 && entry->bytes[0] == 0xa6 && entry->bytes[1] == channel && entry->bytes[2] == (uint8_t)(0xa6 + channel));
}
static void hold_radio_inputs(void) {
    report_nkro_t keys = {.mods = 2};
    memset(keys.bits, 0xff, 12); // Includes keys beyond the six-key slots.
    wireless_driver.send_nkro(&keys);
    report_extra_t extra = {.report_id = REPORT_ID_CONSUMER, .usage = 0xe9};
    wireless_driver.send_extra(&extra);
    extra = (report_extra_t){.report_id = REPORT_ID_SYSTEM, .usage = 0x82};
    wireless_driver.send_extra(&extra);
    report_mouse_t mouse = {.buttons = 3};
    wireless_driver.send_mouse(&mouse);
    event_count = 0;
}
static void test_radio_channel_switch(void) {
    reset(DEVS_BT1);
    hold_radio_inputs();
    wireless_devs_change(DEVS_BT1, DEVS_BT2, false);
    md_main_task();
    assert(event_count == 7);
    assert_radio_releases();
    assert_selector(6, 0x32);
}
static void test_radio_to_usb(void) {
    reset(DEVS_BT1);
    hold_radio_inputs();
    wireless_devs_change(DEVS_BT1, DEVS_USB, false);
    md_main_task();
    assert(event_count == 8);
    assert_radio_releases();
    assert(events[6].kind == EVENT_USB_DRIVER);
    assert_selector(7, 0x11);
}
static void test_usb_to_radio(void) {
    reset(DEVS_USB);
    usb_keyboard.mods  = 2;
    usb_nkro.mods      = 2;
    usb_consumer.usage = usb_system.usage = 3;
    usb_mouse.buttons                     = 3;
    wireless_devs_change(DEVS_USB, DEVS_BT1, false);
    md_main_task();
    const event_kind_t expected[] = {EVENT_USB_KEYBOARD, EVENT_USB_NKRO, EVENT_USB_CONSUMER, EVENT_USB_SYSTEM, EVENT_USB_MOUSE, EVENT_RADIO_DRIVER, EVENT_RADIO_PACKET};
    assert(event_count == sizeof(expected) / sizeof(expected[0]));
    for (unsigned i = 0; i < event_count; ++i)
        assert(events[i].kind == expected[i]);
    assert(usb_keyboard.mods == 0 && usb_nkro.mods == 0 && usb_consumer.usage == 0 && usb_system.usage == 0 && usb_mouse.buttons == 0);
#ifdef KEYBOARD_SHARED_EP
    assert(usb_keyboard.report_id == REPORT_ID_KEYBOARD);
#endif
#ifdef MOUSE_SHARED_EP
    assert(usb_mouse.report_id == REPORT_ID_MOUSE);
#endif
    assert_selector(6, 0x31);
}
static void test_same_channel_wake_retains_input(void) {
    reset(DEVS_BT1);
    hold_radio_inputs();
    power_state = LPWR_STOP;
    wireless_devs_change(DEVS_BT1, DEVS_BT1, false);
    md_main_task();
    assert(event_count == 1 && power_state == LPWR_WAKEUP);
    assert_selector(0, 0x31);
    // The wake key remains held; reselect does not clear the encoder slots.
    report_nkro_t held = {.mods = 2};
    held.bits[0]       = 1U << 4;
    wireless_driver.send_nkro(&held);
    assert(events[1].bytes[1] == 2);
    bool found = false;
    for (unsigned i = 3; i < 9; ++i)
        found |= events[1].bytes[i] == 4;
    assert(found);
}
static void test_peer_does_not_release_master_host(void) {
    reset(DEVS_BT1);
    hold_radio_inputs();
    master = false;
    wireless_devs_change(DEVS_BT1, DEVS_BT2, false);
    assert(event_count == 0 && driver == &wireless_driver);
}
int main(int argc, char **argv) {
    typedef struct {
        const char *name;
        void (*run)(void);
    } switch_test_t;
    const switch_test_t tests[] = {{"radio-channel", test_radio_channel_switch}, {"radio-usb", test_radio_to_usb}, {"usb-radio", test_usb_to_radio}, {"same-channel", test_same_channel_wake_retains_input}, {"peer", test_peer_does_not_release_master_host}};
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (argc == 1 || strcmp(argv[1], tests[i].name) == 0) tests[i].run();
    }
    puts("Host switching: ordered keyboard/media/system/mouse releases and held wake input passed");
}

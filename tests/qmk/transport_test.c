// SPDX-License-Identifier: GPL-2.0-or-later
// Maintained USB suspend logic with simulated host states/time, no USB traffic.
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "usb_main.h"
#include "usb_device_state.h"
#include "wireless.h"

test_usb_driver_t     USB_DRIVER;
host_driver_t         chibios_driver, wireless_driver;
bool                  last_suspend_state;
static host_driver_t *driver;
static uint32_t       now = 1000;
static uint8_t        device, radio_state;
static bool           cable, manual;
static lpwr_state_t   state;
static lpwr_prepare_t preparation;
static unsigned       wakes, waited;
void                  usb_remote_host(void);
uint32_t              sync_timer_read32(void) {
    return now;
}
uint32_t sync_timer_elapsed32(uint32_t timestamp) {
    return now - timestamp;
}
uint8_t wireless_get_current_devs(void) {
    return device;
}
uint8_t *md_getp_state(void) {
    return &radio_state;
}
bool gpio_read_pin(uint32_t pin) {
    assert(pin == HS_BAT_CABLE_PIN);
    return cable;
}
void lpwr_set_timeout_manual(bool requested) {
    manual = requested;
}
lpwr_state_t lpwr_get_state(void) {
    return state;
}
void lpwr_set_state(lpwr_state_t value) {
    state = value;
}
void lpwr_set_prepare_class(lpwr_prepare_t value) {
    preparation = value;
}
host_driver_t *host_get_driver(void) {
    return driver;
}
void host_set_driver(host_driver_t *value) {
    driver = value;
}
void usb_device_state_set_protocol(usb_hid_protocol_t value) {
    assert(value == USB_PROTOCOL_REPORT);
}
void usbWakeupHost(test_usb_driver_t *value) {
    assert(value == &USB_DRIVER);
    ++wakes;
}
void wait_ms(unsigned ms) {
    waited += ms;
}
static void reset(uint8_t mode, uint8_t usb_state, bool present) {
    state       = LPWR_NORMAL;
    preparation = LPWR_PREPARE_ORDINARY;
    device      = mode;
    cable       = present;
    manual      = false;
    wakes = waited = 0;
    // An active USB iteration resets both internal suspend fields.
    uint8_t saved    = device;
    device           = DEVS_USB;
    USB_DRIVER.state = USB_ACTIVE;
    usb_remote_wakeup();
    device            = saved;
    USB_DRIVER.state  = usb_state;
    USB_DRIVER.status = 2;
}
static void test_suspend_deadline(void) {
    const uint8_t states[] = {USB_READY, USB_SUSPENDED};
    for (unsigned i = 0; i < 2; ++i) {
        reset(DEVS_USB, states[i], true);
        usb_remote_wakeup();
        now += 6999;
        usb_remote_wakeup();
        assert(!manual);
        ++now;
        usb_remote_wakeup();
        assert(manual);
        usb_remote_host();
        assert(wakes == (states[i] == USB_SUSPENDED ? 1u : 0u));
        assert(waited == (states[i] == USB_SUSPENDED ? 1000u : 0u));
        usb_remote_host();
        assert(wakes == (states[i] == USB_SUSPENDED ? 1u : 0u));
    }
    reset(DEVS_BT1, USB_SUSPENDED, true);
    usb_remote_wakeup();
    now += 60000;
    usb_remote_wakeup();
    usb_remote_host();
    assert(!manual && wakes == 0 && waited == 0);
    reset(DEVS_USB, USB_SUSPENDED, false);
    usb_remote_wakeup();
    now += 60000;
    usb_remote_wakeup();
    assert(!manual);
    cable = true;
    usb_remote_wakeup();
    now += 6999;
    usb_remote_wakeup();
    assert(!manual);
    ++now;
    usb_remote_wakeup();
    assert(manual);
}
static void test_host_permission_and_resume(void) {
    reset(DEVS_USB, USB_SUSPENDED, true);
    usb_remote_host();
    assert(wakes == 0); // No unconditional wake before deadline.
    state       = LPWR_STOP;
    preparation = LPWR_PREPARE_USB;
    usb_remote_host();
    assert(state == LPWR_WAKEUP && preparation == LPWR_PREPARE_ORDINARY && wakes == 0);
    now += 7000;
    usb_remote_wakeup();
    assert(manual);
    USB_DRIVER.status = 0;
    usb_remote_host();
    assert(wakes == 0);
    USB_DRIVER.status = 2;
    usb_remote_host();
    assert(wakes == 1 && waited == 1000);
    reset(DEVS_USB, USB_SUSPENDED, true);
    now += 7000;
    usb_remote_wakeup();
    assert(manual);
    USB_DRIVER.state = USB_ACTIVE;
    usb_remote_wakeup();
    USB_DRIVER.state = USB_SUSPENDED;
    usb_remote_host();
    assert(wakes == 0);
}
int main(void) {
    test_suspend_deadline();
    test_host_permission_and_resume();
    puts("USB seven-second READY/SUSPENDED gate, BT isolation, cable gate and one-shot remote host wake passed");
}

// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "wireless.h"
#include "usb_main.h"
#include "usb_device_state.h"
#include "transport.h"
#include "split_diagnostics.h"

#ifndef USB_POWER_DOWN_DELAY
#    define USB_POWER_DOWN_DELAY 7000
#endif

static bool     usb_suspend_requested;
static uint32_t usb_suspend_timer;

extern host_driver_t chibios_driver;
extern host_driver_t wireless_driver;

static transport_t transport = TRANSPORT_USB;

void wls_transport_enable(bool enable) __attribute__((weak));
void wls_transport_enable(bool enable) {
    if (enable) {
        if (host_get_driver() != &wireless_driver) {
            host_set_driver(&wireless_driver);
            usb_device_state_set_protocol(USB_PROTOCOL_REPORT);
        }
    } else {
        if (*md_getp_state() == MD_STATE_CONNECTED) {
            wireless_driver.send_keyboard(NULL);
            wireless_driver.send_nkro(NULL);
        }
    }
}

/* Control USB device connection and disconnection by
 * controlling the power supply of the USB DP pull-up resistor.
 * Overwrite these two functions. */
void usb_power_connect(void) __attribute__((weak));
void usb_power_connect(void) {}

void usb_power_disconnect(void) __attribute__((weak));
void usb_power_disconnect(void) {}

void usb_transport_enable(bool enable) __attribute__((weak));
void usb_transport_enable(bool enable) {
    if (enable) {
        if (host_get_driver() != &chibios_driver) {
            extern bool last_suspend_state;

            /* This flag is not set to 1 with probability after usb restart */
            last_suspend_state = true;
#if !defined(KEEP_USB_CONNECTION_IN_WIRELESS_MODE)
            usb_power_connect();
            restart_usb_driver(&USBD1);
#endif
            host_set_driver(&chibios_driver);
        }
    } else {
        if (USB_DRIVER.state == USB_ACTIVE) {
            chibios_driver.send_keyboard(NULL);
            chibios_driver.send_nkro(NULL);
        }

#if !defined(KEEP_USB_CONNECTION_IN_WIRELESS_MODE)
        usbStop(&USBD1);
        usbDisconnectBus(&USBD1);
        usb_power_disconnect();
#endif
    }
}

void set_transport(transport_t new_transport) {
    if (transport != new_transport) {
        transport = new_transport;

        switch (transport) {
            case TRANSPORT_USB: {
                wls_transport_enable(false);
                usb_transport_enable(true);
            } break;
            case TRANSPORT_WLS: {
                usb_transport_enable(false);
                wls_transport_enable(true);
            } break;
            default:
                break;
        }
    }
}

transport_t get_transport(void) {
    return transport;
}

void usb_remote_wakeup(void) {
#ifdef USB_REMOTE_USE_QMK
    if (USB_DRIVER.state == USB_SUSPENDED) {
        dprintln("suspending keyboard");
        while (USB_DRIVER.state == USB_SUSPENDED) {
            /* Do this in the suspended state */
            suspend_power_down(); // on AVR this deep sleeps for 15ms
            /* Remote wakeup */
            if ((USB_DRIVER.status & 2U) && suspend_wakeup_condition()) {
                usbWakeupHost(&USB_DRIVER);
#    if USB_SUSPEND_WAKEUP_DELAY > 0
                // Some hubs, kvm switches, and monitors do
                // weird things, with USB device state bouncing
                // around wildly on wakeup, yielding race
                // conditions that can corrupt the keyboard state.
                //
                // Pause for a while to let things settle...
                wait_ms(USB_SUSPEND_WAKEUP_DELAY);
#    endif
            }
        }
        /* Woken up */
    }
#else
    // V15 0x080032F0: USB READY/SUSPENDED plus cable, then ordinary manual sleep.
    bool usb       = wireless_get_current_devs() == DEVS_USB;
    bool suspended = USB_DRIVER.state == USB_SUSPENDED || USB_DRIVER.state == USB_READY;
    if (usb && suspended && gpio_read_pin(HS_BAT_CABLE_PIN)) {
        if (!usb_suspend_timer) usb_suspend_timer = sync_timer_read32();
        if (sync_timer_elapsed32(usb_suspend_timer) >= USB_POWER_DOWN_DELAY) {
            usb_suspend_timer     = sync_timer_read32();
            usb_suspend_requested = true;
            split_diagnostics_event(SPLIT_DIAG_SLEEP_REASON, SPLIT_DIAG_HOST_SUSPEND, 0, 0);
            lpwr_set_timeout_manual(true);
        }
    } else {
        usb_suspend_timer = sync_timer_read32();
        if (usb && !suspended) usb_suspend_requested = false;
    }
#endif
}

#ifndef USB_REMOTE_USE_QMK
void usb_remote_host(void) {
    if (wireless_get_current_devs() != DEVS_USB) return;
    // V15's wired key hook wakes PRE/STOP and resets the active class before
    // deciding whether a suspended host can receive a remote-wake request.
    if (lpwr_get_state() == LPWR_PRESLEEP || lpwr_get_state() == LPWR_STOP) {
        lpwr_set_state(LPWR_WAKEUP);
        lpwr_set_prepare_class(LPWR_PREPARE_ORDINARY);
    }
    if (USB_DRIVER.state == USB_SUSPENDED) {
        if ((USB_DRIVER.status & 2U) && usb_suspend_requested && lpwr_get_state() <= LPWR_WAKEUP) {
            usb_suspend_requested = false;
            usb_suspend_timer     = sync_timer_read32();
            usbWakeupHost(&USB_DRIVER);
#    if USB_SUSPEND_WAKEUP_DELAY > 0
            // Some hubs, kvm switches, and monitors do
            // weird things, with USB device state bouncing
            // around wildly on wakeup, yielding race
            // conditions that can corrupt the keyboard state.
            //
            // Pause for a while to let things settle...
            wait_ms(USB_SUSPEND_WAKEUP_DELAY);
#    endif
        }
    }
}

#endif

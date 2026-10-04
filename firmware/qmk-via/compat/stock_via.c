// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "via.h"
#include "raw_hid.h"
#include "split_diagnostics.h"
#ifdef GMK70_DEBUG_USB_BOOTLOADER
#    include "usb_main.h"
#    include "usb_endpoints.h"
#    include "wireless.h"
#    include "wait.h"
#    ifdef GMK70_SPLIT_UPDATE
#        include "update/qmk_update.h"
#    endif
#endif

// The factory definition calls effect 45 "Close All". Keep the upstream
// brightness scaling (0..255 on the wire, 0..80 at the LEDs) and other effects.
bool via_command_kb(uint8_t *data, uint8_t length) {
#ifdef GMK70_DEBUG_USB_BOOTLOADER
#    ifdef GMK70_SPLIT_UPDATE
    if (gmk_update_usb(data, length)) {
        raw_hid_send(data, length);
        return true;
    }
#    endif
    if (length == RAW_EPSIZE && data[0] == id_bootloader_jump) {
        bool allowed = is_keyboard_master() && get_transport() == TRANSPORT_USB && USB_DRIVER.state == USB_ACTIVE;
        for (uint8_t i = 1; i < length; ++i) {
            if (data[i] != 0) allowed = false;
        }
        if (!allowed) data[0] = id_unhandled;
        raw_hid_send(data, length);
        if (allowed) {
            // Allow the acknowledgement to leave USB before resetting. This runs
            // only for this explicit command; no polling or EEPROM invalidation.
            wait_ms(100);
            reset_keyboard();
        }
        return true;
    }
#endif
#ifdef GMK70_SPLIT_DIAGNOSTICS
    if (split_diagnostics_command(data, length)) {
        raw_hid_send(data, length);
        return true;
    }
#endif
    if (length >= 4 && data[0] == id_custom_set_value && data[1] == id_qmk_rgb_matrix_channel && data[2] == id_qmk_rgb_matrix_effect && data[3] == 45) {
        data[3] = 0;
        via_qmk_rgb_matrix_command(data, length);
        data[3] = 45;
        raw_hid_send(data, length);
        return true;
    }
    return false;
}

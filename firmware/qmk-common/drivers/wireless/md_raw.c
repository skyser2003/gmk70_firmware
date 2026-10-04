// Copyright 2024 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef RAW_ENABLE

#    include "quantum.h"
#    include "wireless.h"
#    include "usb_endpoints.h"
#    include "raw_hid.h"
#    include "md_raw.h"
#    ifdef GMK70_DEBUG_USB_BOOTLOADER
#        include "via.h"
#    endif

void __wrap_raw_hid_send(uint8_t *data, uint8_t length) {
    if (length != RAW_EPSIZE) {
        return;
    }

    if (get_transport() == TRANSPORT_USB) {
        __real_raw_hid_send(data, length);
    } else {
        md_send_raw(data, length);
    }
}

void md_receive_raw_cb(uint8_t *data, uint8_t length) {
    if (length == RAW_EPSIZE) {
#    ifdef GMK70_DEBUG_USB_BOOTLOADER
        // Radio raw HID shares VIA's parser. Never forward the debug-only USB
        // bootloader command, even if the physical selector currently says USB.
        if (data[0] == id_bootloader_jump
#        ifdef GMK70_SPLIT_UPDATE
            || data[0] == 0x7F
#        endif
        ) {
            data[0] = id_unhandled;
            md_send_raw(data, length);
            return;
        }
#    endif
        raw_hid_receive(data, length);
    }
}

#endif

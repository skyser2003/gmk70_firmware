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
        // The optional split updater retains its direct-USB gate. Ordinary
        // stock VIA commands, including 0x0B, share the normal radio parser.
#        ifdef GMK70_SPLIT_UPDATE
        if (data[0] == 0x7F) {
            data[0] = id_unhandled;
            md_send_raw(data, length);
            return;
        }
#        endif
#    endif
        raw_hid_receive(data, length);
    }
}

#endif

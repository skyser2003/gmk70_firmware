// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "via.h"
#include "raw_hid.h"
#include "usb_endpoints.h"
#include "split_diagnostics.h"
#include "stock_v15.h"
#ifdef GMK70_DEBUG_USB_BOOTLOADER
#    include "usb_main.h"
#    include "usb_endpoints.h"
#    include "wireless.h"
#    include "wait.h"
#    ifdef GMK70_SPLIT_UPDATE
#        include "update/qmk_update.h"
#    endif
#endif

// Recovered V15 behavior takes precedence over labels in the factory JSON.
bool via_command_kb(uint8_t *data, uint8_t length) {
    if (length != RAW_EPSIZE) return false;
    gmk70_stock_activity();
    switch (data[0]) {
        case 0x01:
            data[1] = 0;
            data[2] = 12;
            raw_hid_send(data, length);
            return true;
        case 0x0A:
            gmk70_stock_request_reset();
            raw_hid_send(data, length);
            return true;
        case 0x0B:
            gmk70_stock_request_bootloader();
            data[1] = 1;
            raw_hid_send(data, length);
            return true;
        case 0x21:
            if (data[1])
                gmk70_stock_nkro_set(data[2]);
            else
                data[1] = gmk70_stock_nkro_get();
            raw_hid_send(data, length);
            return true;
        default:
            break;
    }
    if ((data[0] == 7 || data[0] == 8) && data[1] == 0 && data[2] == 1) {
        if (data[0] == 7)
            gmk70_stock_sleep_set(data[3]);
        else
            data[3] = gmk70_stock_sleep_get();
        raw_hid_send(data, length);
        return true;
    }
    if ((data[0] == 7 || data[0] == 8) && data[1] == 3 && data[2] == 1) {
        if (data[0] == 7)
            gmk70_stock_brightness_set(data[3]);
        else
            data[3] = gmk70_stock_brightness_get();
        raw_hid_send(data, length);
        return true;
    }
#ifdef GMK70_DEBUG_USB_BOOTLOADER
#    ifdef GMK70_SPLIT_UPDATE
    if (gmk_update_usb(data, length)) {
        raw_hid_send(data, length);
        return true;
    }
#    endif
#endif
#ifdef GMK70_SPLIT_DIAGNOSTICS
    if (split_diagnostics_command(data, length)) {
        raw_hid_send(data, length);
        return true;
    }
#endif
    if (data[0] == id_custom_set_value && data[1] == id_qmk_rgb_matrix_channel && data[2] == id_qmk_rgb_matrix_effect && data[3] > 44) {
        const uint8_t requested = data[3];
        data[3]                 = 44;
        via_qmk_rgb_matrix_command(data, length);
        data[3] = requested;
        raw_hid_send(data, length);
        return true;
    }
    return false;
}

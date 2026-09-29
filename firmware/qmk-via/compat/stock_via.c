// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "via.h"
#include "raw_hid.h"
#include "battery_diagnostics.h"

// The factory definition calls effect 45 "Close All". Keep the upstream
// brightness scaling (0..255 on the wire, 0..80 at the LEDs) and other effects.
bool via_command_kb(uint8_t *data, uint8_t length) {
#ifdef GMK70_BATTERY_DIAGNOSTICS
    if (battery_diagnostics_command(data, length)) {
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

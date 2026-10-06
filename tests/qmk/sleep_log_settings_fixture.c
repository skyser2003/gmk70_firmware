// SPDX-License-Identifier: GPL-2.0-or-later
// A separate translation unit exercises settings' external flash references.
#include "flash.h"
void sleep_log_test_settings_io(void) {
    uint8_t data[16] = {0};
    flash_read_range(0, data, sizeof(data));
    flash_write_range(0, data, sizeof(data));
    flash_erase_block(0);
}

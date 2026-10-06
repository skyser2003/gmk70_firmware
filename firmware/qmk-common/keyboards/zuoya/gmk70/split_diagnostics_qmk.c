// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H
#include "split_diagnostics.h"
#include "wireless.h"
#include "module.h"
#include "split_util.h"
#include "transactions.h"

split_diag_sample_t split_diagnostics_sample(void) {
    extern bool lower_sleep;
    return (split_diag_sample_t){
        // Same role predicate as split_role.c, without its GPIO configuration write.
        .status = lpwr_get_state() | (lpwr_get_timeout_manual() << 2) | (gpio_read_pin(A5) << 3) | (gpio_read_pin(A8) << 4) | (lower_sleep << 6) | ((gpio_read_pin(A7) || gpio_read_pin(B9)) << 7), .device = wireless_get_current_devs(), .radio = *md_getp_state(), .wake = lpwr_get_sleep_wakeupcd(), .pins = gpio_read_pin(A7) | (gpio_read_pin(A15) << 1) | (gpio_read_pin(C15) << 2) | (gpio_read_pin(C14) << 3) | (gpio_read_pin(B9) << 4), .left = is_keyboard_left(), .connected = is_transport_connected(), .battery = *md_getp_bat(), .sleep_selector = gmk70_stock_sleep_get(), .timeout = lpwr_timeout_value_read(),
    };
}

bool __real_transaction_rpc_exec(int8_t transaction_id, uint8_t request_length, const void *request, uint8_t response_length, void *response);

bool __wrap_transaction_rpc_exec(int8_t transaction_id, uint8_t request_length, const void *request, uint8_t response_length, void *response) {
    if (transaction_id != USER_SYNC_MMS || request_length != 5 || response_length != 5) {
        return __real_transaction_rpc_exec(transaction_id, request_length, request, response_length, response);
    }
    const uint8_t command = ((const uint8_t *)request)[0];
    split_diagnostics_rpc_begin(command);
    const bool success = __real_transaction_rpc_exec(transaction_id, request_length, request, response_length, response);
    // Use the four unused bytes of the existing two-second mode reply. The
    // battery-fix rollback removes command 0x56 and its periodic traffic.
    const uint8_t *reply    = response;
    const bool     mode_ack = success && command == 0x55 && reply[0] == 0;
    split_diagnostics_rpc_end(command, success, mode_ack ? reply + 1 : NULL);
    return success;
}

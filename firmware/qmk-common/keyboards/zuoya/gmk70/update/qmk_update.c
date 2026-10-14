// SPDX-License-Identifier: GPL-2.0-or-later
#include "qmk_update.h"
#include "protocol.h"
#ifndef GMK70_VIAL_UPDATE
#    include "eeprom_query.h"
#endif
#ifdef GMK70_PERSISTENT_DIAGNOSTICS
#    include "sleep_log_query.h"
#endif
#include "quantum.h"
#include "transactions.h"
#include "wireless.h"
#include "usb_main.h"
#include "bootloader.h"
#include <string.h>

static gmk_update_t  update;
static uint8_t       queued[GMK_PACKET_SIZE];
static uint8_t       last_response[GMK_PACKET_SIZE];
static volatile bool pending;
static uint32_t      last_request;
static uint32_t      reset_at;

bool gmk_update_active(void) {
    return pending || update.state == GMK_RECEIVING || update.state == GMK_SEALED || update.state == GMK_COMMITTED;
}

// The split transport calls this in its high-priority thread. Only copy RAM here;
// flash programming, EEPROM reads and CRC checks run in normal housekeeping.
// DATA fills the existing page buffer immediately; only a full/final page queues.
static void receive(const uint8_t *request, uint8_t *response) {
    chSysLock();
    if (!gmk_packet_valid(request)) {
        memcpy(response, request, GMK_PACKET_SIZE);
        response[5] = GMK_BAD_REQUEST;
    } else if (request[4] == GMK_STATUS || pending) {
        memcpy(response, pending ? queued : last_response, GMK_PACKET_SIZE);
        if (pending) response[5] = GMK_BUSY;
    } else if (request[4] == GMK_DATA) {
        gmk_update_result_t result = wireless_get_current_devs() == DEVS_USB ? gmk_update_buffer_data(&update, request) : GMK_UNAVAILABLE;
        gmk_update_response(&update, request, result, response);
        response[7] = (is_keyboard_left() ? 1 : 0) | (is_keyboard_master() ? 2 : 0);
        if (result == GMK_OK || result == GMK_BUSY) last_request = timer_read32();
        if (result == GMK_BUSY) {
            memcpy(queued, request, GMK_PACKET_SIZE);
            pending = true;
        } else {
            memcpy(last_response, response, GMK_PACKET_SIZE);
        }
    } else {
        memcpy(queued, request, GMK_PACKET_SIZE);
        pending = true;
        memcpy(response, request, GMK_PACKET_SIZE);
        response[5] = GMK_BUSY;
    }
    chSysUnlock();
}

static void peer_handler(uint8_t in_size, const void *in, uint8_t out_size, void *out) {
    if (in_size != GMK_PACKET_SIZE || out_size != GMK_PACKET_SIZE) return;
    const uint8_t *request  = in;
    uint8_t       *response = out;
    if (is_keyboard_master() || is_keyboard_left() || request[3] != 1) {
        memcpy(response, request, GMK_PACKET_SIZE);
        response[5] = GMK_UNAVAILABLE;
        return;
    }
    receive(request, response);
}

void gmk_update_init(void) {
    update                                 = (gmk_update_t){0};
    pending                                = false;
    last_request                           = 0;
    reset_at                               = 0;
    const uint8_t request[GMK_PACKET_SIZE] = {GMK_UPDATE_COMMAND, 0x70, GMK_UPDATE_ABI, !is_keyboard_left(), GMK_STATUS};
    gmk_update_response(&update, request, GMK_OK, last_response);
    transaction_register_rpc(GMK_UPDATE_RPC, peer_handler);
}

void gmk_update_task(void) {
    if (update.state == GMK_COMMITTED) {
        if (timer_elapsed32(reset_at) >= 300) mcu_reset();
        return;
    }
    chSysLock();
    if (!pending) {
        if (update.state != GMK_IDLE && timer_elapsed32(last_request) > 10000) update = (gmk_update_t){0};
        chSysUnlock();
        return;
    }
    chSysUnlock();
    uint8_t request[GMK_PACKET_SIZE];
    chSysLock();
    memcpy(request, queued, GMK_PACKET_SIZE);
    chSysUnlock();
    bool                eeprom_query = request[4] == GMK_EEPROM_INFO || request[4] == GMK_EEPROM_READ;
    bool                log_query    = request[4] == GMK_SLEEP_LOG_INFO || request[4] == GMK_SLEEP_LOG_READ;
    gmk_update_result_t result       = GMK_UNAVAILABLE;
    uint8_t             response[GMK_PACKET_SIZE];
    if (eeprom_query) {
#ifndef GMK70_VIAL_UPDATE
        if (wireless_get_current_devs() == DEVS_USB) result = update.state == GMK_IDLE ? GMK_OK : GMK_BAD_STATE;
        gmk_eeprom_query(request, result, response);
#else
        memset(response, 0, sizeof(response));
        memcpy(response, request, 5);
        memcpy(response + 8, request + 8, 8);
        response[5] = GMK_UNAVAILABLE;
#endif
    } else if (log_query) {
#ifdef GMK70_PERSISTENT_DIAGNOSTICS
        if (wireless_get_current_devs() == DEVS_USB) result = update.state == GMK_IDLE ? GMK_OK : GMK_BAD_STATE;
        gmk_sleep_log_query(request, result, response);
#else
        memset(response, 0, sizeof(response));
        memcpy(response, request, 5);
        memcpy(response + 8, request + 8, 8);
        response[5] = GMK_UNAVAILABLE;
#endif
    } else {
        if (wireless_get_current_devs() == DEVS_USB) result = gmk_update_process(&update, request);
        gmk_update_response(&update, request, result, response);
    }
    response[7] = (is_keyboard_left() ? 1 : 0) | (is_keyboard_master() ? 2 : 0);
    if (request[4] == GMK_INFO && result == GMK_OK) {
        // Explicit INFO only; no periodic integrity scan or background traffic.
        gmk_write_u32(response + 28, gmk_crc32(gmk_flash_view(GMK_FLASH_BASE), GMK_BOOT_SIZE));
        uint32_t size = gmk_running_size();
        gmk_write_u32(response + 16, size);
        gmk_write_u32(response + 20, gmk_crc32(gmk_flash_view(GMK_APP_BASE), size));
    }
    // A rejected settings query must not keep an unfinished update alive.
    if (!eeprom_query && !log_query) last_request = timer_read32();
    if (update.state == GMK_COMMITTED) reset_at = last_request;
    chSysLock();
    memcpy(last_response, response, sizeof(response));
    pending = false;
    chSysUnlock();
}

#if defined(GMK70_DEBUG_USB_BOOTLOADER) || defined(GMK70_VIAL_UPDATE)
bool gmk_update_usb(uint8_t *data, uint8_t length) {
    if (length != GMK_PACKET_SIZE || data[0] != GMK_UPDATE_COMMAND) return false;
    uint8_t response[GMK_PACKET_SIZE];
    memcpy(response, data, sizeof(response));
    response[5] = GMK_UNAVAILABLE;
    if (gmk_packet_valid(data) && is_keyboard_left() && is_keyboard_master() && get_transport() == TRANSPORT_USB && USB_DRIVER.state == USB_ACTIVE) {
#    ifdef GMK70_VIAL_UPDATE
        // USB enumeration alone does not mean the mode switch is wired.
        bool wired = wireless_get_current_devs() == DEVS_USB;
        if (!wired || !gmk_update_usb_authorized(data[4])) {
            response[5] = wired ? GMK_LOCKED : GMK_UNAVAILABLE;
            memcpy(data, response, sizeof(response));
            return true;
        }
#    endif
        if (data[3] == 0) {
            receive(data, response);
        } else if (!transaction_rpc_exec(GMK_UPDATE_RPC, GMK_PACKET_SIZE, data, GMK_PACKET_SIZE, response)) {
            memcpy(response, data, sizeof(response));
            response[5] = GMK_UNAVAILABLE;
        }
    }
    memcpy(data, response, sizeof(response));
    return true;
}
#endif

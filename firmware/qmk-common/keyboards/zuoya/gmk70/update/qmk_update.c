// SPDX-License-Identifier: GPL-2.0-or-later
#include "qmk_update.h"
#include "protocol.h"
#ifdef GMK70_VIAL_TRIAL
#    ifndef GMK70_VIAL_UPDATE
#        error "The trial receiver requires native Vial authorization"
#    endif
#    include "trial.h"
#    include "trial_platform.h"
#    include "trial_peer.h"
static bool               trial_reset_pending;
static bool               trial_mode;
static gmk_trial_launch_t trial_launch;
#endif

static bool packet_valid(const uint8_t request[GMK_PACKET_SIZE]) {
#ifdef GMK70_VIAL_TRIAL
    if (gmk_trial_packet_valid(request)) return true;
#endif
    return gmk_packet_valid(request);
}
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
#ifdef GMK70_VIAL_TRIAL
    if (trial_reset_pending || gmk_trial_peer_active(timer_read32())) return true;
#endif
    return pending || update.state == GMK_RECEIVING || update.state == GMK_SEALED || update.state == GMK_COMMITTED;
}

static void update_response(const uint8_t request[GMK_PACKET_SIZE], gmk_update_result_t result, uint8_t response[GMK_PACKET_SIZE]) {
    gmk_update_response(&update, request, result, response);
#ifdef GMK70_VIAL_TRIAL
    if (request[1] == GMK_TRIAL_NAMESPACE) gmk_write_u32(response + 24, GMK_TRIAL_ABI);
    if (request[1] == GMK_TRIAL_NAMESPACE && (request[4] == GMK_TRIAL_WATCHDOG || request[4] == GMK_TRIAL_RESULT || request[4] == GMK_TRIAL_CAPS || request[4] == GMK_TRIAL_FALLBACK_TEST) && result == GMK_OK) {
        // These operations have no transfer session; still correlate their ACK.
        memcpy(response + 8, request + 8, 8);
    }
#endif
}

// The split transport calls this in its high-priority thread. Only copy RAM here;
// flash programming, EEPROM reads and CRC checks run in normal housekeeping.
// DATA fills the existing page buffer immediately; only a full/final page queues.
static void receive(const uint8_t *request, uint8_t *response) {
    chSysLock();
    if (!packet_valid(request)) {
        memcpy(response, request, GMK_PACKET_SIZE);
        response[5] = GMK_BAD_REQUEST;
#ifdef GMK70_VIAL_TRIAL
    } else if (trial_reset_pending && (request[1] != GMK_TRIAL_NAMESPACE || request[4] != GMK_STATUS)) {
        update_response(request, GMK_BAD_STATE, response);
#endif
    } else if (pending) {
        // Housekeeping mutates update outside this lock. While pending, only
        // queued/cached packets are published; never inspect transfer state.
#ifdef GMK70_VIAL_TRIAL
        if (request[1] != queued[1]) {
            memcpy(response, last_response, GMK_PACKET_SIZE);
            memcpy(response, request, 5);
            memcpy(response + 8, request + 8, 8);
            response[5] = GMK_BAD_STATE;
        } else
#endif
        {
            memcpy(response, queued, GMK_PACKET_SIZE);
            response[5] = GMK_BUSY;
        }
#ifdef GMK70_VIAL_TRIAL
    } else if (update.state != GMK_IDLE && request[4] != GMK_INFO && request[4] != GMK_STATUS && request[4] != GMK_TRIAL_RESULT && request[4] != GMK_TRIAL_CAPS && (request[1] == GMK_TRIAL_NAMESPACE) != trial_mode) {
        update_response(request, GMK_BAD_STATE, response);
    } else if (request[4] == GMK_STATUS && request[1] != last_response[1]) {
        update_response(request, GMK_OK, response);
#endif
    } else if (request[4] == GMK_STATUS) {
        memcpy(response, last_response, GMK_PACKET_SIZE);
    } else if (request[4] == GMK_DATA) {
        gmk_update_result_t result = GMK_UNAVAILABLE;
        if (wireless_get_current_devs() == DEVS_USB) {
#ifdef GMK70_VIAL_TRIAL
            if (request[1] == GMK_TRIAL_NAMESPACE)
                result = gmk_trial_buffer_data(&update, request);
            else
#endif
                result = gmk_update_buffer_data(&update, request);
        }
        update_response(request, result, response);
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
#ifdef GMK70_VIAL_TRIAL
    gmk_trial_peer_init();
#    ifdef GMK70_TRIAL_RUNTIME
    gmk_trial_runtime_init();
#    endif
    trial_reset_pending = false;
    trial_mode          = false;
    trial_launch        = (gmk_trial_launch_t){0};
#endif
    update                                 = (gmk_update_t){0};
    pending                                = false;
    last_request                           = 0;
    reset_at                               = 0;
    const uint8_t request[GMK_PACKET_SIZE] = {GMK_UPDATE_COMMAND, 0x70, GMK_UPDATE_ABI, !is_keyboard_left(), GMK_STATUS};
    gmk_update_response(&update, request, GMK_OK, last_response);
    transaction_register_rpc(GMK_UPDATE_RPC, peer_handler);
}

void gmk_update_task(void) {
#ifdef GMK70_VIAL_TRIAL
    if (trial_reset_pending) {
        if (timer_elapsed32(reset_at) >= 300) gmk_trial_platform_reset();
        return;
    }
#endif
    if (update.state == GMK_COMMITTED) {
        if (timer_elapsed32(reset_at) >= 300) mcu_reset();
        return;
    }
    chSysLock();
    if (!pending) {
        if (update.state != GMK_IDLE && timer_elapsed32(last_request) > 10000) {
            update = (gmk_update_t){0};
#ifdef GMK70_VIAL_TRIAL
            trial_launch = (gmk_trial_launch_t){0};
            trial_mode   = false;
#endif
        }
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
        if (wireless_get_current_devs() == DEVS_USB) {
#ifdef GMK70_VIAL_TRIAL
            bool trial = request[1] == GMK_TRIAL_NAMESPACE;
            if (trial) {
                bool recovery_ready = true;
#    ifdef GMK70_TRIAL_RUNTIME
                recovery_ready = is_keyboard_left() && request[4] != GMK_TRIAL_RUN;
                gmk_trial_runtime_fallback_test(request[4] == GMK_TRIAL_FALLBACK_TEST);
#    else
                recovery_ready = request[4] != GMK_TRIAL_FALLBACK_TEST;
#    endif
                result = gmk_trial_process(&update, &trial_launch, request, recovery_ready);
                if (result == GMK_OK && (request[4] == GMK_TRIAL_RUN || request[4] == GMK_TRIAL_WATCHDOG || request[4] == GMK_TRIAL_FALLBACK_TEST)) {
                    gmk_image_t image  = {0};
                    bool        ready  = request[4] != GMK_TRIAL_RUN || gmk_trial_take_launch(&trial_launch, &image);
                    uint32_t    action = request[4] == GMK_TRIAL_RUN ? GMK_TRIAL_BOOT_RUN : GMK_TRIAL_BOOT_PROBE;
                    if (ready && gmk_trial_platform_prepare(action, image, !is_keyboard_left())) {
                        reset_at            = timer_read32();
                        trial_reset_pending = true;
                    } else {
                        result = GMK_UNAVAILABLE;
                    }
                }
            } else
#endif
                result = gmk_update_process(&update, request);
#ifdef GMK70_VIAL_TRIAL
            if (request[4] == GMK_BEGIN && result == GMK_OK) trial_mode = trial;
#endif
        }
        update_response(request, result, response);
    }
#ifdef GMK70_VIAL_TRIAL
    if (request[1] == GMK_TRIAL_NAMESPACE && request[4] == GMK_TRIAL_CAPS && result == GMK_OK) {
#    ifdef GMK70_TRIAL_RUNTIME
        gmk_write_u32(response + 16, is_keyboard_left() ? 25 : 0); // Left-only qualification.
        gmk_write_u32(response + 20, is_keyboard_left() ? 12000 : 0);
#    else
        gmk_write_u32(response + 16, 7); // PROBE, staging RUN, early startup.
        gmk_write_u32(response + 20, 0);
#    endif
        gmk_write_u32(response + 24, GMK_TRIAL_RAM_BASE);
        gmk_write_u32(response + 28, GMK_TRIAL_RAM_END);
    }
    if (request[1] == GMK_TRIAL_NAMESPACE && request[4] == GMK_TRIAL_RESULT && result == GMK_OK) gmk_trial_platform_result(response + 16);
#endif
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
    if (packet_valid(data) && is_keyboard_left() && is_keyboard_master() && get_transport() == TRANSPORT_USB && USB_DRIVER.state == USB_ACTIVE) {
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
        } else {
            bool delivered = transaction_rpc_exec(GMK_UPDATE_RPC, GMK_PACKET_SIZE, data, GMK_PACKET_SIZE, response);
            if (!delivered) {
                memcpy(response, data, sizeof(response));
                response[5] = GMK_UNAVAILABLE;
            }
#    ifdef GMK70_VIAL_TRIAL
            else if (data[1] == GMK_TRIAL_NAMESPACE && (data[4] == GMK_TRIAL_RUN || data[4] == GMK_TRIAL_WATCHDOG) && response[1] == GMK_TRIAL_NAMESPACE && response[4] == data[4] && (response[5] == GMK_OK || response[5] == GMK_BUSY)) {
                gmk_trial_peer_start(timer_read32());
            }
#    endif
        }
    }
    memcpy(data, response, sizeof(response));
    return true;
}
#endif

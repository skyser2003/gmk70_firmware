// SPDX-License-Identifier: GPL-2.0-or-later
#include "trial.h"
#include <string.h>

bool gmk_trial_packet_valid(const uint8_t request[GMK_PACKET_SIZE]) {
    if (request[1] != GMK_TRIAL_NAMESPACE || request[2] != GMK_TRIAL_ABI) return false;
    uint8_t normal[GMK_PACKET_SIZE];
    memcpy(normal, request, sizeof(normal));
    normal[1] = 0x70;
    normal[2] = GMK_UPDATE_ABI;
    switch (request[4]) {
        case GMK_BEGIN:
            if (request[5] != 16) return false;
            normal[5] = 12;
            memset(normal + 28, 0, 4);
            break;
        case GMK_TRIAL_WATCHDOG:
        case GMK_TRIAL_RESULT:
        case GMK_TRIAL_RUN:
        case GMK_TRIAL_FALLBACK_TEST:
            normal[4] = GMK_SEAL;
            break;
        case GMK_TRIAL_CAPS:
            // Unlike INFO, CAPS carries a fresh nonce in both words.
            normal[4]  = GMK_SEAL;
            normal[12] = normal[13] = normal[14] = normal[15] = 0;
            break;
        case GMK_STATUS:
        case GMK_DATA:
        case GMK_SEAL:
        case GMK_ABORT:
        case GMK_INFO:
            break;
        default:
            return false;
    }
    return gmk_packet_valid(normal);
}

bool gmk_trial_vectors(const uint8_t *data, uint32_t size) {
    if (!gmk_image_bounds(size)) return false;
    uint32_t stack = gmk_read_u32(data);
    if (stack <= GMK_TRIAL_RAM_BASE || stack > GMK_TRIAL_RAM_END || stack % 8) return false;
    for (unsigned i = 1; i < 16; ++i) {
        uint32_t vector = gmk_read_u32(data + 4 * i);
        if (!vector && i > 3) continue;
        if (!(vector & 1U) || vector < GMK_STAGE_BASE + 64 || (vector & ~1U) > GMK_STAGE_BASE + size - 2) return false;
    }
    return true;
}

static bool commit_erased(void) {
    const uint8_t *page = gmk_flash_view(GMK_COMMIT_BASE);
    for (unsigned i = 0; i < GMK_PAGE_SIZE; ++i)
        if (page[i] != 0xFF) return false;
    return true;
}

static bool trial_valid(gmk_image_t image) {
    if (!gmk_image_bounds(image.size)) return false;
    const uint8_t *data = gmk_flash_view(GMK_STAGE_BASE);
    return gmk_trial_vectors(data, image.size) && gmk_crc32(data, image.size) == image.crc;
}

gmk_update_result_t gmk_trial_buffer_data(gmk_update_t *update, const uint8_t request[GMK_PACKET_SIZE]) {
    if (!gmk_trial_packet_valid(request) || request[4] != GMK_DATA) return GMK_BAD_REQUEST;
    uint8_t normal[GMK_PACKET_SIZE];
    memcpy(normal, request, sizeof(normal));
    normal[1] = 0x70;
    normal[2] = GMK_UPDATE_ABI;
    return gmk_update_buffer_data(update, normal);
}

gmk_update_result_t gmk_trial_process(gmk_update_t *update, gmk_trial_launch_t *launch, const uint8_t request[GMK_PACKET_SIZE], bool recovery_ready) {
    if (!gmk_trial_packet_valid(request)) return GMK_BAD_REQUEST;
    uint8_t  op      = request[4];
    uint32_t session = gmk_read_u32(request + 8);
    if (op == GMK_STATUS || op == GMK_INFO || op == GMK_TRIAL_RESULT || op == GMK_TRIAL_CAPS) return GMK_OK;
    if (op == GMK_TRIAL_WATCHDOG || op == GMK_TRIAL_FALLBACK_TEST) {
        if (!session || update->state != GMK_IDLE || launch->armed || !commit_erased()) return GMK_BAD_STATE;
        return recovery_ready ? GMK_OK : GMK_UNAVAILABLE;
    }
    if (update->state == GMK_COMMITTED) return GMK_BAD_STATE;
    if (op == GMK_BEGIN) {
        gmk_image_t image = {gmk_read_u32(request + 16), gmk_read_u32(request + 20)};
        if (!session || !gmk_image_bounds(image.size) || gmk_read_u32(request + 24) != GMK_STAGE_BASE || gmk_read_u32(request + 28) != GMK_TRIAL_RAM_END) return GMK_BAD_IMAGE;
        if (!commit_erased()) return GMK_BAD_STATE;
        if (update->state != GMK_IDLE) return update->state == GMK_RECEIVING && update->session == session && update->image.size == image.size && update->image.crc == image.crc ? GMK_OK : GMK_BAD_STATE;
        *update = (gmk_update_t){.state = GMK_RECEIVING, .image = image, .session = session};
        memset(update->page, 0xFF, GMK_PAGE_SIZE);
        *launch = (gmk_trial_launch_t){0};
        return GMK_OK;
    }
    if (!session || session != update->session) return GMK_BAD_STATE;
    if (op == GMK_ABORT) {
        *update = (gmk_update_t){0};
        *launch = (gmk_trial_launch_t){0};
        return GMK_OK;
    }
    if (op == GMK_DATA) {
        uint8_t normal[GMK_PACKET_SIZE];
        memcpy(normal, request, sizeof(normal));
        normal[1] = 0x70;
        normal[2] = GMK_UPDATE_ABI;
        return gmk_update_process(update, normal);
    }
    if (op == GMK_SEAL) {
        if (update->state != GMK_RECEIVING && update->state != GMK_SEALED) return GMK_BAD_STATE;
        if (update->received != update->image.size || update->staged != update->received) return GMK_BAD_STATE;
        if (!commit_erased() || !trial_valid(update->image)) {
            update->state = GMK_FAILED;
            return GMK_BAD_IMAGE;
        }
        update->state = GMK_SEALED;
        return GMK_OK;
    }
    if (op == GMK_TRIAL_RUN) {
        if (update->state != GMK_SEALED || launch->consumed) return GMK_BAD_STATE;
        if (!commit_erased() || !trial_valid(update->image)) return GMK_BAD_IMAGE;
        if (!recovery_ready) return GMK_UNAVAILABLE;
        *launch = (gmk_trial_launch_t){.image = update->image, .armed = true};
        return GMK_OK;
    }
    return GMK_BAD_REQUEST;
}

bool gmk_trial_take_launch(gmk_trial_launch_t *launch, gmk_image_t *image) {
    if (!launch->armed || launch->consumed) return false;
    launch->armed    = false;
    launch->consumed = true;
    if (!commit_erased() || !trial_valid(launch->image)) return false;
    *image = launch->image;
    return true;
}

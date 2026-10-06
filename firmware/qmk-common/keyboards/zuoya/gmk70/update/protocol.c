// SPDX-License-Identifier: GPL-2.0-or-later
#include "protocol.h"
#include <string.h>

bool gmk_packet_valid(const uint8_t request[GMK_PACKET_SIZE]) {
    if (request[0] != GMK_UPDATE_COMMAND || request[1] != 0x70 || request[2] != GMK_UPDATE_ABI || request[3] > 1 || request[4] < GMK_STATUS || request[4] > GMK_SLEEP_LOG_READ || request[5] > GMK_DATA_SIZE || request[6] || request[7]) return false;
    uint8_t count = request[5];
    if (request[4] >= GMK_EEPROM_INFO) {
        if (!gmk_read_u32(request + 8)) return false;
        bool info = request[4] == GMK_EEPROM_INFO || request[4] == GMK_SLEEP_LOG_INFO;
        if (info && (count || gmk_read_u32(request + 12))) return false;
        if (!info && !count) return false;
        // READ's count describes the response; it carries no request payload.
        for (unsigned i = 16; i < GMK_PACKET_SIZE; ++i)
            if (request[i]) return false;
        return true;
    }
    if ((request[4] == GMK_BEGIN && count != 12) || (request[4] == GMK_DATA && count == 0) || (request[4] != GMK_BEGIN && request[4] != GMK_DATA && count != 0)) return false;
    for (unsigned i = 16 + count; i < GMK_PACKET_SIZE; ++i)
        if (request[i]) return false;
    return request[4] == GMK_DATA || gmk_read_u32(request + 12) == 0;
}

static gmk_update_result_t flash_failure(gmk_update_t *update) {
    update->state = GMK_FAILED;
    return GMK_FLASH_ERROR;
}

gmk_update_result_t gmk_update_process(gmk_update_t *update, const uint8_t request[GMK_PACKET_SIZE]) {
    // EEPROM queries belong to the read-only QMK service, never the installer.
    if (!gmk_packet_valid(request) || request[4] > GMK_INFO) return GMK_BAD_REQUEST;
    uint32_t session = gmk_read_u32(request + 8);
    uint32_t offset  = gmk_read_u32(request + 12);
    uint8_t  op      = request[4];
    if (op == GMK_INFO || op == GMK_STATUS) return GMK_OK;
    if (update->state == GMK_COMMITTED) return op == GMK_INSTALL && session == update->session ? GMK_OK : GMK_BAD_STATE;
    if (op == GMK_BEGIN) {
        gmk_image_t image = {gmk_read_u32(request + 16), gmk_read_u32(request + 20)};
        if (!session || !gmk_image_bounds(image.size) || gmk_read_u32(request + 24) != GMK_UPDATE_ABI) return GMK_BAD_IMAGE;
        if (update->state != GMK_IDLE) {
            return update->state == GMK_RECEIVING && update->session == session && update->image.size == image.size && update->image.crc == image.crc ? GMK_OK : GMK_BAD_STATE;
        }
        if (!gmk_manifest_clear()) return flash_failure(update);
        *update = (gmk_update_t){.state = GMK_RECEIVING, .image = image, .session = session};
        memset(update->page, 0xFF, GMK_PAGE_SIZE);
        return GMK_OK;
    }
    if (!session || session != update->session) return GMK_BAD_STATE;
    if (op == GMK_ABORT) {
        *update = (gmk_update_t){0};
        return GMK_OK;
    }
    if (op == GMK_DATA) {
        uint32_t count = request[5];
        if (update->state != GMK_RECEIVING || offset > update->image.size || count > update->image.size - offset || count > GMK_PAGE_SIZE - offset % GMK_PAGE_SIZE) return GMK_BAD_OFFSET;
        if (offset < update->received && count <= update->received - offset) {
            uint32_t       buffered = update->received - update->received % GMK_PAGE_SIZE;
            const uint8_t *previous = offset >= buffered ? update->page + offset % GMK_PAGE_SIZE : gmk_flash_view(GMK_STAGE_BASE + offset);
            return memcmp(previous, request + 16, count) == 0 ? GMK_OK : GMK_BAD_OFFSET;
        }
        if (offset != update->received) return GMK_BAD_OFFSET;
        memcpy(update->page + offset % GMK_PAGE_SIZE, request + 16, count);
        update->received += count;
        if (update->received % GMK_PAGE_SIZE == 0 || update->received == update->image.size) {
            if (!gmk_flash_page(GMK_STAGE_BASE + offset - offset % GMK_PAGE_SIZE, update->page)) return flash_failure(update);
            // Preserve the final partial page for idempotent retransmission.
            if (update->received % GMK_PAGE_SIZE == 0) memset(update->page, 0xFF, GMK_PAGE_SIZE);
        }
        return GMK_OK;
    }
    if (op == GMK_SEAL) {
        if (update->state == GMK_SEALED) return GMK_OK;
        if (update->state != GMK_RECEIVING || update->received != update->image.size) return GMK_BAD_STATE;
        if (!gmk_image_valid(GMK_STAGE_BASE, update->image)) {
            update->state = GMK_FAILED;
            return GMK_BAD_IMAGE;
        }
        update->state = GMK_SEALED;
        return GMK_OK;
    }
    if (op == GMK_INSTALL) {
        if (update->state != GMK_SEALED) return GMK_BAD_STATE;
        // Recheck immediately before making the install durable.
        if (!gmk_image_valid(GMK_STAGE_BASE, update->image)) return GMK_BAD_IMAGE;
        uint8_t manifest[GMK_PAGE_SIZE];
        gmk_manifest_encode(manifest, update->image);
        if (!gmk_flash_page(GMK_COMMIT_BASE, manifest)) return flash_failure(update);
        update->state = GMK_COMMITTED;
        return GMK_OK;
    }
    return GMK_BAD_REQUEST;
}

void gmk_update_response(const gmk_update_t *update, const uint8_t request[GMK_PACKET_SIZE], gmk_update_result_t result, uint8_t response[GMK_PACKET_SIZE]) {
    memset(response, 0, GMK_PACKET_SIZE);
    memcpy(response, request, 5);
    response[5] = result;
    response[6] = update->state;
    gmk_write_u32(response + 8, update->session);
    gmk_write_u32(response + 12, update->received);
    gmk_write_u32(response + 16, update->image.size);
    gmk_write_u32(response + 20, update->image.crc);
    gmk_write_u32(response + 24, GMK_UPDATE_ABI);
}

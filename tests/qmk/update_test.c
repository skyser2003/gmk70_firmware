// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t  flash_memory[0x40000];
static uint8_t  payload[1284];
static uint8_t  previous[sizeof(payload)];
static int      writes;
static int      fail_write = -1;
static unsigned tear_bytes;
static bool     installer;

const uint8_t *gmk_flash_view(uint32_t address) {
    assert(address >= GMK_FLASH_BASE && address < GMK_FLASH_BASE + sizeof(flash_memory));
    return flash_memory + address - GMK_FLASH_BASE;
}

bool gmk_flash_page(uint32_t address, const uint8_t data[GMK_PAGE_SIZE]) {
    uint32_t base = installer ? GMK_APP_BASE : GMK_STAGE_BASE;
    assert((address >= base && address < base + GMK_APP_SIZE) || address == GMK_COMMIT_BASE);
    assert(address % GMK_PAGE_SIZE == 0);
    uint8_t *target = flash_memory + address - GMK_FLASH_BASE;
    memset(target, 0xFF, GMK_PAGE_SIZE);
    if (writes++ == fail_write) {
        memcpy(target, data, tear_bytes);
        return false;
    }
    memcpy(target, data, GMK_PAGE_SIZE);
    return true;
}

static void request(uint8_t packet[32], uint8_t op, uint32_t session) {
    memset(packet, 0, 32);
    packet[0] = GMK_UPDATE_COMMAND;
    packet[1] = 0x70;
    packet[2] = GMK_UPDATE_ABI;
    packet[4] = op;
    gmk_write_u32(packet + 8, session);
}

static void reset_flash(void) {
    memset(flash_memory, 0xFF, sizeof(flash_memory));
    for (unsigned i = 0; i < sizeof(payload); ++i)
        payload[i] = (uint8_t)(i * 13);
    gmk_write_u32(payload, GMK_RAM_END - 8);
    gmk_write_u32(payload + 4, GMK_APP_BASE + 513);
    memset(previous, 0xA5, sizeof(previous));
    memcpy(flash_memory + GMK_APP_BASE - GMK_FLASH_BASE, previous, sizeof(previous));
    writes     = 0;
    fail_write = -1;
    tear_bytes = 0;
    installer  = false;
}

static gmk_update_t stage_image(void) {
    gmk_update_t update = {0};
    uint8_t      packet[32];
    request(packet, GMK_BEGIN, 1234);
    packet[5] = 12;
    gmk_write_u32(packet + 16, sizeof(payload));
    gmk_write_u32(packet + 20, gmk_crc32(payload, sizeof(payload)));
    gmk_write_u32(packet + 24, GMK_UPDATE_ABI);
    assert(gmk_update_process(&update, packet) == GMK_OK);
    for (unsigned offset = 0; offset < sizeof(payload);) {
        unsigned count = sizeof(payload) - offset;
        if (count > 16) count = 16;
        request(packet, GMK_DATA, 1234);
        packet[5] = count;
        gmk_write_u32(packet + 12, offset);
        memcpy(packet + 16, payload + offset, count);
        assert(gmk_update_process(&update, packet) == GMK_OK);
        int before = writes;
        assert(gmk_update_process(&update, packet) == GMK_OK);
        assert(writes == before);
        offset += count;
    }
    assert(memcmp(gmk_flash_view(GMK_APP_BASE), previous, sizeof(previous)) == 0);
    request(packet, GMK_SEAL, 1234);
    assert(gmk_update_process(&update, packet) == GMK_OK);
    request(packet, GMK_INSTALL, 1234);
    assert(gmk_update_process(&update, packet) == GMK_OK);
    assert(update.state == GMK_COMMITTED);
    return update;
}

static void test_install_and_resume(void) {
    // Interrupt every destination page at every byte boundary, then reboot.
    for (int page = 0; page < 6; ++page) {
        for (unsigned byte = 0; byte <= GMK_PAGE_SIZE; ++byte) {
            reset_flash();
            gmk_update_t update = stage_image();
            installer           = true;
            fail_write          = writes + page;
            tear_bytes          = byte;
            assert(!gmk_install_pending());
            fail_write = -1;
            assert(gmk_install_pending());
            assert(gmk_image_valid(GMK_APP_BASE, update.image));
            assert(gmk_install_pending());
        }
    }
}

static void test_torn_commit(void) {
    for (unsigned byte = 0; byte <= GMK_PAGE_SIZE; ++byte) {
        reset_flash();
        gmk_update_t update = stage_image();
        uint8_t      commit[GMK_PAGE_SIZE];
        gmk_manifest_encode(commit, update.image);
        uint8_t *page = flash_memory + GMK_COMMIT_BASE - GMK_FLASH_BASE;
        memset(page, 0xFF, GMK_PAGE_SIZE);
        memcpy(page, commit, byte);
        installer = true;
        assert(gmk_install_pending());
        assert(memcmp(gmk_flash_view(GMK_APP_BASE), previous, sizeof(previous)) == 0 || gmk_image_valid(GMK_APP_BASE, update.image));
    }
    // Torn commit invalidation can only happen after the new app is complete.
    for (unsigned byte = 0; byte < 32; ++byte) {
        reset_flash();
        gmk_update_t update = stage_image();
        installer           = true;
        fail_write          = writes + 6;
        tear_bytes          = byte;
        assert(!gmk_install_pending());
        assert(gmk_image_valid(GMK_APP_BASE, update.image));
        fail_write = -1;
        assert(gmk_install_pending());
    }
}

static void test_rejections(void) {
    reset_flash();
    assert(gmk_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926);
    assert(!gmk_image_bounds(UINT32_MAX));
    assert(!gmk_image_bounds(GMK_APP_SIZE + 4));
    uint8_t      packet[32];
    gmk_update_t update = {0};
    for (unsigned op = 0; op < 256; ++op) {
        request(packet, op, 1);
        if (op == GMK_STATUS || op == GMK_INFO) continue;
        assert(gmk_update_process(&update, packet) != GMK_OK);
    }
    assert(writes == 0);
    // Read requests are valid extension packets, but cannot reach flash work in
    // the portable update state machine or resident installer.
    request(packet, GMK_EEPROM_INFO, 123);
    assert(gmk_packet_valid(packet) && gmk_update_process(&update, packet) == GMK_BAD_REQUEST);
    request(packet, GMK_EEPROM_READ, 124);
    packet[5] = 16;
    gmk_write_u32(packet + 12, 774);
    assert(gmk_packet_valid(packet) && gmk_update_process(&update, packet) == GMK_BAD_REQUEST && writes == 0);
    request(packet, GMK_SLEEP_LOG_INFO, 125);
    assert(gmk_packet_valid(packet) && gmk_update_process(&update, packet) == GMK_BAD_REQUEST);
    request(packet, GMK_SLEEP_LOG_READ, 126);
    packet[5] = 16;
    gmk_write_u32(packet + 12, 8192 - 16);
    assert(gmk_packet_valid(packet) && gmk_update_process(&update, packet) == GMK_BAD_REQUEST && writes == 0);
    update    = stage_image();
    installer = true;
    flash_memory[GMK_STAGE_BASE - GMK_FLASH_BASE + 700] ^= 1;
    int before = writes;
    assert(!gmk_install_pending());
    assert(writes == before);
    assert(memcmp(gmk_flash_view(GMK_APP_BASE), previous, sizeof(previous)) == 0);
    request(packet, GMK_ABORT, 1234);
    assert(gmk_update_process(&update, packet) == GMK_BAD_STATE);
}

static void test_transfer_failure_and_padding(void) {
    reset_flash();
    uint8_t      packet[32];
    gmk_update_t update = {0};
    request(packet, GMK_BEGIN, 3);
    packet[5] = 12;
    gmk_write_u32(packet + 16, sizeof(payload));
    gmk_write_u32(packet + 20, gmk_crc32(payload, sizeof(payload)));
    gmk_write_u32(packet + 24, GMK_UPDATE_ABI);
    packet[31] = 1;
    assert(gmk_update_process(&update, packet) == GMK_BAD_REQUEST);
    packet[31] = 0;
    assert(gmk_update_process(&update, packet) == GMK_OK);
    request(packet, GMK_DATA, 3);
    packet[5] = 16;
    memcpy(packet + 16, payload, 16);
    gmk_write_u32(packet + 12, UINT32_MAX);
    assert(gmk_update_process(&update, packet) == GMK_BAD_OFFSET);
    gmk_write_u32(packet + 12, 0);
    assert(gmk_update_process(&update, packet) == GMK_OK);
    packet[16] ^= 1;
    assert(gmk_update_process(&update, packet) == GMK_BAD_OFFSET);
    request(packet, GMK_SEAL, 3);
    assert(gmk_update_process(&update, packet) == GMK_BAD_STATE);
    request(packet, GMK_INSTALL, 3);
    assert(gmk_update_process(&update, packet) == GMK_BAD_STATE);
    request(packet, GMK_ABORT, 4);
    assert(gmk_update_process(&update, packet) == GMK_BAD_STATE);
    request(packet, GMK_ABORT, 3);
    assert(gmk_update_process(&update, packet) == GMK_OK);
    assert(update.state == GMK_IDLE);
    assert(memcmp(gmk_flash_view(GMK_APP_BASE), previous, sizeof(previous)) == 0);
}

int main(void) {
    test_install_and_resume();
    test_torn_commit();
    test_rejections();
    test_transfer_failure_and_padding();
    puts("Updater: transfer validation, corruption, and 1,831 power-cut cases passed");
}

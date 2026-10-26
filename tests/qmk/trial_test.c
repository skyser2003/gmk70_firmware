// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/trial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t  flash_memory[0x40000], original[0x20000], payload[1284];
static unsigned writes;
static int      fail_page;
const uint8_t  *gmk_flash_view(uint32_t address) {
    assert(address >= GMK_FLASH_BASE && address < GMK_FLASH_BASE + sizeof(flash_memory));
    return flash_memory + address - GMK_FLASH_BASE;
}
bool gmk_flash_page(uint32_t address, const uint8_t data[GMK_PAGE_SIZE]) {
    assert(address >= GMK_STAGE_BASE && address < GMK_STAGE_BASE + GMK_APP_SIZE && address % GMK_PAGE_SIZE == 0);
    ++writes;
    uint8_t *dest = flash_memory + address - GMK_FLASH_BASE;
    memset(dest, 0xFF, GMK_PAGE_SIZE);
    if ((int)(address - GMK_STAGE_BASE) / GMK_PAGE_SIZE == fail_page) {
        memcpy(dest, data, 7);
        return false;
    }
    memcpy(dest, data, GMK_PAGE_SIZE);
    return true;
}
static void fixture(void) {
    memset(flash_memory, 0xFF, sizeof(flash_memory));
    memset(flash_memory, 0xA5, sizeof(original));
    memcpy(original, flash_memory, sizeof(original));
    memset(payload, 0, sizeof(payload));
    gmk_write_u32(payload, GMK_TRIAL_RAM_END);
    for (unsigned i = 1; i < 7; ++i)
        gmk_write_u32(payload + i * 4, GMK_STAGE_BASE + 513);
    for (unsigned i = 11; i < 16; ++i)
        gmk_write_u32(payload + i * 4, GMK_STAGE_BASE + 513);
    writes    = 0;
    fail_page = -1;
}
static void request(uint8_t packet[32], uint8_t op) {
    memset(packet, 0, 32);
    packet[0] = GMK_UPDATE_COMMAND;
    packet[1] = GMK_TRIAL_NAMESPACE;
    packet[2] = 1;
    packet[4] = op;
    gmk_write_u32(packet + 8, 42);
    if (op == GMK_BEGIN) {
        packet[5] = 16;
        gmk_write_u32(packet + 16, sizeof(payload));
        gmk_write_u32(packet + 20, gmk_crc32(payload, sizeof(payload)));
        gmk_write_u32(packet + 24, GMK_STAGE_BASE);
        gmk_write_u32(packet + 28, GMK_TRIAL_RAM_END);
    }
}
static gmk_update_t stage(gmk_trial_launch_t *launch) {
    gmk_update_t update = {0};
    uint8_t      packet[32];
    request(packet, GMK_BEGIN);
    assert(gmk_trial_process(&update, launch, packet, false) == GMK_OK);
    assert(writes == 0);
    for (unsigned offset = 0; offset < sizeof(payload);) {
        unsigned count = sizeof(payload) - offset;
        if (count > 16) count = 16;
        request(packet, GMK_DATA);
        packet[5] = count;
        gmk_write_u32(packet + 12, offset);
        memcpy(packet + 16, payload + offset, count);
        assert(gmk_trial_process(&update, launch, packet, false) == GMK_OK);
        unsigned before = writes;
        assert(gmk_trial_process(&update, launch, packet, false) == GMK_OK);
        assert(writes == before);
        offset += count;
    }
    return update;
}
static void preserved(void) {
    assert(memcmp(flash_memory, original, sizeof(original)) == 0);
    for (unsigned i = GMK_STAGE_BASE - GMK_FLASH_BASE + GMK_APP_SIZE; i < sizeof(flash_memory); ++i)
        assert(flash_memory[i] == 0xFF);
}
static void test_separation_and_one_shot(void) {
    fixture();
    gmk_trial_launch_t launch = {0};
    gmk_update_t       update = stage(&launch);
    uint8_t            packet[32];
    request(packet, GMK_SEAL);
    assert(gmk_trial_process(&update, &launch, packet, false) == GMK_OK);
    assert(!gmk_image_valid(GMK_STAGE_BASE, update.image)); // ordinary INSTALL cannot accept trial vectors
    request(packet, GMK_INSTALL);
    assert(!gmk_trial_packet_valid(packet));
    assert(gmk_trial_process(&update, &launch, packet, true) == GMK_BAD_REQUEST);
    assert(gmk_update_process(&update, packet) == GMK_BAD_REQUEST);
    packet[1] = 0x70;
    assert(gmk_update_process(&update, packet) == GMK_BAD_IMAGE);
    request(packet, GMK_TRIAL_RUN);
    assert(gmk_trial_process(&update, &launch, packet, false) == GMK_UNAVAILABLE);
    gmk_image_t image;
    assert(!gmk_trial_take_launch(&launch, &image));
    assert(gmk_trial_process(&update, &launch, packet, true) == GMK_OK);
    assert(gmk_trial_process(&update, &launch, packet, true) == GMK_OK); // duplicate before consumption
    assert(gmk_trial_take_launch(&launch, &image));
    assert(image.crc == update.image.crc);
    assert(!gmk_trial_take_launch(&launch, &image));
    assert(gmk_trial_process(&update, &launch, packet, true) == GMK_BAD_STATE);
    assert(gmk_install_pending());
    preserved();
}
static void test_vectors_and_commit(void) {
    fixture();
    assert(gmk_trial_vectors(payload, sizeof(payload)));
    const uint32_t stacks[] = {GMK_RAM_BASE, GMK_TRIAL_RAM_END + 8, GMK_TRIAL_RAM_END - 4, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(stacks) / sizeof(stacks[0]); ++i) {
        gmk_write_u32(payload, stacks[i]);
        assert(!gmk_trial_vectors(payload, sizeof(payload)));
    }
    gmk_write_u32(payload, GMK_TRIAL_RAM_END);
    const uint32_t resets[] = {0, GMK_APP_BASE + 513, GMK_STAGE_BASE + 512, GMK_STAGE_BASE + sizeof(payload) + 1, GMK_STAGE_BASE + 1};
    for (unsigned i = 0; i < sizeof(resets) / sizeof(resets[0]); ++i) {
        gmk_write_u32(payload + 4, resets[i]);
        assert(!gmk_trial_vectors(payload, sizeof(payload)));
    }
    assert(!gmk_trial_vectors(payload, 0));
    assert(!gmk_trial_vectors(payload, UINT32_MAX));
    for (unsigned i = 0; i < GMK_PAGE_SIZE; ++i) {
        fixture();
        flash_memory[GMK_COMMIT_BASE - GMK_FLASH_BASE + i] = 0;
        gmk_update_t       update                          = {0};
        gmk_trial_launch_t launch                          = {0};
        uint8_t            packet[32];
        request(packet, GMK_BEGIN);
        assert(gmk_trial_process(&update, &launch, packet, false) == GMK_BAD_STATE);
        assert(writes == 0);
    }
}
static void test_failures(void) {
    for (int page = 0; page < 6; ++page) {
        fixture();
        fail_page                 = page;
        gmk_update_t       update = {0};
        gmk_trial_launch_t launch = {0};
        uint8_t            packet[32];
        request(packet, GMK_BEGIN);
        assert(gmk_trial_process(&update, &launch, packet, false) == GMK_OK);
        for (unsigned offset = 0; offset < sizeof(payload);) {
            unsigned count = sizeof(payload) - offset;
            if (count > 16) count = 16;
            request(packet, GMK_DATA);
            packet[5] = count;
            gmk_write_u32(packet + 12, offset);
            memcpy(packet + 16, payload + offset, count);
            gmk_update_result_t result = gmk_trial_process(&update, &launch, packet, false);
            if (result == GMK_FLASH_ERROR) break;
            assert(result == GMK_OK);
            offset += count;
        }
        assert(update.state == GMK_FAILED);
        assert(gmk_install_pending());
        preserved();
    }
    fixture();
    gmk_trial_launch_t launch = {0};
    gmk_update_t       update = stage(&launch);
    uint8_t            packet[32];
    flash_memory[GMK_STAGE_BASE - GMK_FLASH_BASE + 700] ^= 1;
    request(packet, GMK_SEAL);
    assert(gmk_trial_process(&update, &launch, packet, false) == GMK_BAD_IMAGE);
    preserved();
    fixture();
    update = stage(&launch);
    request(packet, GMK_SEAL);
    assert(gmk_trial_process(&update, &launch, packet, false) == GMK_OK);
    request(packet, GMK_TRIAL_RUN);
    assert(gmk_trial_process(&update, &launch, packet, true) == GMK_OK);
    flash_memory[GMK_STAGE_BASE - GMK_FLASH_BASE + 700] ^= 1;
    gmk_image_t image;
    assert(!gmk_trial_take_launch(&launch, &image));
    assert(!gmk_trial_take_launch(&launch, &image));
    preserved();
    fixture();
    update = (gmk_update_t){0};
    request(packet, GMK_BEGIN);
    assert(gmk_trial_process(&update, &launch, packet, false) == GMK_OK);
    request(packet, GMK_SEAL);
    assert(gmk_trial_process(&update, &launch, packet, false) == GMK_BAD_STATE);
    request(packet, GMK_TRIAL_RUN);
    assert(gmk_trial_process(&update, &launch, packet, true) == GMK_BAD_STATE);
    request(packet, GMK_ABORT);
    assert(gmk_trial_process(&update, &launch, packet, false) == GMK_OK);
    assert(update.state == GMK_IDLE);
    preserved();
}
int main(void) {
    test_separation_and_one_shot();
    test_vectors_and_commit();
    test_failures();
    puts("trial tests passed");
}

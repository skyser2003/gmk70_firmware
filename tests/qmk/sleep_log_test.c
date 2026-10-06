// SPDX-License-Identifier: GPL-2.0-or-later
// NOR programming, reboot, and power cuts execute the maintained journal code.
#include "sleep_log.h"
#include "split_diagnostics.h"
#include "update/sleep_log_query.h"
#include "ch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static uint8_t             flash[SLEEP_LOG_BASE + SLEEP_LOG_SIZE];
static long                budget;
static long                erase_budget;
static bool                available;
static syssts_t            locks;
static unsigned            writes, erases;
static uint32_t            now    = 60000;
static split_diag_sample_t sample = {.status = 0xC2, .device = 1, .radio = 2, .battery = 0, .sleep_selector = 7, .timeout = 300000, .left = true, .connected = true};

const uint8_t *gmk_flash_view(uint32_t address) {
    (void)address;
    assert(false);
    return NULL;
}
bool gmk_flash_page(uint32_t address, const uint8_t data[256]) {
    (void)address;
    (void)data;
    assert(false);
    return false;
}

syssts_t chSysGetStatusAndLockX(void) {
    return locks++;
}
void chSysRestoreStatusX(syssts_t previous) {
    assert(locks == previous + 1);
    locks = previous;
}
uint32_t timer_read32(void) {
    return now;
}
split_diag_sample_t split_diagnostics_sample(void) {
    assert(locks);
    return sample;
}
bool sleep_log_storage_init(void) {
    return available;
}
uint32_t sleep_log_storage_id(void) {
    return available ? 0xC22013 : 0;
}
static void check(uint32_t address, size_t size) {
    assert(!locks && address >= SLEEP_LOG_BASE && address < sizeof(flash) && size <= sizeof(flash) - address);
}
bool sleep_log_storage_read(uint32_t address, void *data, size_t size) {
    check(address, size);
    memcpy(data, flash + address, size);
    return true;
}
bool sleep_log_storage_write(uint32_t address, const void *data, size_t size) {
    check(address, size);
    ++writes;
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        if (!budget) return false;
        --budget;
        assert((flash[address + i] & bytes[i]) == bytes[i]);
        flash[address + i] &= bytes[i];
    }
    return true;
}
bool sleep_log_storage_erase(uint32_t address) {
    check(address, SLEEP_LOG_SECTOR);
    assert(!(address % SLEEP_LOG_SECTOR));
    ++erases;
    for (size_t i = 0; i < SLEEP_LOG_SECTOR; ++i) {
        if (!erase_budget) return false;
        --erase_budget;
        flash[address + i] = 0xFF;
    }
    return true;
}
static void fixture(void) {
    memset(flash, 0xFF, sizeof(flash));
    memset(flash, 0x5A, SLEEP_LOG_BASE); // Settings erase block must stay untouched.
    available = true;
    budget = erase_budget = LONG_MAX;
    writes = erases = 0;
    sleep_log_init();
    assert(!writes && !erases);
}
static void page(uint8_t data[256], uint32_t uptime) {
    memset(data, 0xFF, 256);
    memcpy(data + 12, &uptime, 4);
    data[38] = 0;
    data[39] = 12;
}
static unsigned valid_records(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < SLEEP_LOG_SIZE; i += 256)
        count += sleep_log_valid(flash + SLEEP_LOG_BASE + i);
    return count;
}
static bool find_time(uint32_t time) {
    for (unsigned i = 0; i < SLEEP_LOG_SIZE; i += 256) {
        uint8_t *record = flash + SLEEP_LOG_BASE + i;
        uint32_t saved;
        memcpy(&saved, record + 12, 4);
        if (sleep_log_valid(record) && saved == time) return true;
    }
    return false;
}
static void test_power_cuts(void) {
    uint8_t data[256];
    for (long cut = 0; cut <= 260; ++cut) {
        fixture();
        page(data, 123);
        assert(sleep_log_append(data));
        budget = cut;
        page(data, 456);
        bool done = sleep_log_append(data);
        assert(done == (cut == 260));
        budget = LONG_MAX;
        sleep_log_init(); // Cold RAM state, same NOR contents.
        assert(find_time(123));
        assert(find_time(456) == done);
        page(data, 789);
        assert(sleep_log_append(data) && find_time(789));
    }
    puts("PASS: journal survives every byte cut in data/commit programming");
}
static void test_rotation_and_erase_cut(void) {
    fixture();
    uint8_t data[256];
    for (unsigned i = 1; i <= 70; ++i) {
        page(data, i);
        assert(sleep_log_append(data));
        if (i % 3 == 0) sleep_log_init();
        assert(find_time(i));
    }
    for (unsigned i = 0; i < SLEEP_LOG_BASE; ++i)
        assert(flash[i] == 0x5A);
    assert(erases && valid_records() >= 16);
    for (long cut = 0; cut <= SLEEP_LOG_SECTOR; cut += 128) {
        fixture();
        for (unsigned i = 1; i <= 32; ++i) {
            page(data, i);
            assert(sleep_log_append(data));
        }
        erase_budget = cut;
        page(data, 33);
        sleep_log_append(data);
        sleep_log_init();
        assert(find_time(32)); // Newest record is in the other sector.
        for (unsigned i = 0; i < SLEEP_LOG_BASE; ++i)
            assert(flash[i] == 0x5A);
    }
    puts("PASS: journal rotation and interrupted erase preserve newest/settings");
}
static void test_unknown_storage_and_corruption(void) {
    fixture();
    flash[SLEEP_LOG_BASE] = 0x12;
    sleep_log_init();
    uint8_t data[256];
    page(data, 1);
    assert(!sleep_log_append(data) && !writes && !erases);
    assert(!(sleep_log_status() & SLEEP_LOG_OWNED));
    fixture();
    page(data, 1);
    assert(sleep_log_append(data));
    page(data, 2);
    assert(sleep_log_append(data));
    flash[SLEEP_LOG_BASE + 256 + 100] ^= 1;
    sleep_log_init();
    assert(find_time(1) && !find_time(2));
    page(data, 3);
    assert(sleep_log_append(data) && find_time(3));
    available = false;
    sleep_log_init();
    assert(!sleep_log_append(data) && !sleep_log_read(0, data, sizeof(data)));
    puts("PASS: unknown/corrupt/unavailable storage does not overwrite data");
}
static void test_checkpoints_and_query(void) {
    fixture();
    split_diagnostics_event(SPLIT_DIAG_SLEEP_REASON, 3, 0, 0);
    split_diagnostics_sleep_accepted(true, 60001, 1);
    split_diagnostics_event(SPLIT_DIAG_STATE, 0, 1, 1);
    split_diagnostics_event(SPLIT_DIAG_STATE, 1, 2, 0);
    split_diagnostics_checkpoint(SLEEP_LOG_BEFORE_STOP);
    unsigned before = writes;
    for (unsigned i = 0; i < 100; ++i)
        split_diagnostics_checkpoint(SLEEP_LOG_BEFORE_STOP);
    assert(writes == before && valid_records() == 1);
    uint8_t *saved = flash + SLEEP_LOG_BASE;
    uint32_t timeout, idle;
    memcpy(&timeout, saved + 20, 4);
    memcpy(&idle, saved + 24, 4);
    assert(timeout == 300000 && idle == 60001 && saved[28] == 0 && saved[29] == 7 && saved[36] == 3 && saved[53]);
    sample.status = 3;
    split_diagnostics_checkpoint(SLEEP_LOG_AFTER_WAKE);
    split_diagnostics_checkpoint(SLEEP_LOG_AFTER_WAKE);
    assert(valid_records() == 2);
    sleep_log_init(); // Booting does not write or replace previous records.
    assert(writes == before + 2);
    uint8_t request[32] = {GMK_UPDATE_COMMAND, 0x70, 1, 0, GMK_SLEEP_LOG_INFO}, response[32];
    gmk_write_u32(request + 8, 123);
    unsigned original_writes = writes, original_erases = erases;
    gmk_sleep_log_query(request, GMK_OK, response);
    assert(response[5] == GMK_OK && gmk_read_u32(response + 16) == 8192);
    assert(gmk_read_u32(response + 20) == sleep_log_crc(flash + SLEEP_LOG_BASE, SLEEP_LOG_SIZE));
    request[4] = GMK_SLEEP_LOG_READ;
    request[5] = 16;
    for (uint32_t offset = 0; offset < SLEEP_LOG_SIZE; offset += 16) {
        gmk_write_u32(request + 12, offset);
        gmk_sleep_log_query(request, GMK_OK, response);
        assert(response[5] == GMK_OK && response[6] == 16 && !memcmp(response + 16, flash + SLEEP_LOG_BASE + offset, 16));
    }
    gmk_write_u32(request + 12, SLEEP_LOG_SIZE - 1);
    gmk_sleep_log_query(request, GMK_OK, response);
    assert(response[5] == GMK_BAD_OFFSET);
    gmk_sleep_log_query(request, GMK_BAD_STATE, response);
    assert(response[5] == GMK_BAD_STATE);
    assert(writes == original_writes && erases == original_erases);
    assert(locks == 0);
    split_diagnostics_event(SPLIT_DIAG_STATE, 3, 0, 0);
    sample.status = 0;
    split_diagnostics_rpc_end(0x55, true, NULL);
    split_diagnostics_rpc_end(0x55, false, NULL);
    split_diagnostics_loop();
    unsigned saved_writes = writes;
    assert(saved_writes == original_writes + 2);
    for (unsigned i = 0; i < 100; ++i) {
        split_diagnostics_rpc_end(0x55, false, NULL);
        split_diagnostics_loop();
    }
    assert(writes == saved_writes);
    puts("PASS: checkpoints retain forced 60-second cause, deduplicate STOP, and queries are read-only");
}
int main(void) {
    test_power_cuts();
    test_rotation_and_erase_cut();
    test_unknown_storage_and_corruption();
    test_checkpoints_and_query();
}

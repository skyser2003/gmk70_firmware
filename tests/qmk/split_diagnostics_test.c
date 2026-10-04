// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ch.h"
#include "split_diagnostics.h"

static uint32_t            now = 1000;
static syssts_t            lock_depth;
static split_diag_sample_t sample = {.status = 0x98, .device = 1, .radio = 2, .connected = true, .left = true};
static unsigned            replies;

bool via_command_kb(uint8_t *data, uint8_t length);
void via_qmk_rgb_matrix_command(uint8_t *data, uint8_t length) {
    (void)data;
    (void)length;
    assert(false); // Split getters must never enter an RGB setter.
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    assert(!lock_depth); // USB transmission must happen outside the recorder lock.
    assert(length == 32 && data[0] == 2 && data[1] == 0x81);
    assert(data[3] == 'G' && data[4] == 'S' && data[5] == 2);
    ++replies;
}

syssts_t chSysGetStatusAndLockX(void) {
    return lock_depth++;
}
void chSysRestoreStatusX(syssts_t status) {
    assert(lock_depth == status + 1);
    lock_depth = status;
}
uint32_t timer_read32(void) {
    return now;
}
split_diag_sample_t split_diagnostics_sample(void) {
    assert(lock_depth);
    return sample;
}

static uint32_t get_u32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

static void read_page(uint8_t data[32], uint8_t page, uint32_t sequence) {
    memset(data, 0, 32);
    data[0] = 2;
    data[1] = 0x81;
    data[2] = page;
    for (uint8_t i = 0; i < 4; ++i)
        data[3 + i] = sequence >> (24 - 8 * i);
    const unsigned before = replies;
    assert(via_command_kb(data, 32));
    assert(replies == before + 1);
    assert(data[3] == 'G' && data[4] == 'S' && data[5] == 2);
    assert(!lock_depth);
}

static void test_getter_boundary(void) {
    uint8_t data[33] = {2, 0x81};
    for (uint8_t length = 0; length <= 33; ++length) {
        if (length != 32) assert(!via_command_kb(data, length));
    }
    for (unsigned command = 0; command <= UINT8_MAX; ++command) {
        data[0] = command;
        if (command != 2) assert(!via_command_kb(data, 32));
    }
    data[0] = 2;
    for (uint8_t i = 3; i < 32; ++i) {
        data[i] = 1;
        assert(!via_command_kb(data, 32));
        data[i] = 0;
    }
    data[2] = 4;
    assert(!via_command_kb(data, 32));
    data[2] = 3;
    data[7] = 1;
    assert(!via_command_kb(data, 32));
    assert(replies == 0);
}

static void test_peer_observes_stalled_main(void) {
    uint8_t reply[4], data[32];
    split_diagnostics_loop();
    now += 512;
    // Model the split worker pre-empting a blocked main thread.
    split_diagnostics_reply(reply);
    assert(reply[0] == 0xD1 && reply[1] == sample.status);
    assert(reply[2] == 32 && reply[3] == 0);
    split_diagnostics_rpc_begin(0x55);
    split_diagnostics_rpc_end(0x55, true, reply);
    read_page(data, 2, 0);
    assert(get_u32(data + 6) == now && memcmp(data + 10, reply, 4) == 0 && data[14] == 1);
    split_diagnostics_loop();
    read_page(data, 1, 0);
    // Legacy SPI slots remain reserved and cannot imply actual measurements.
    assert(get_u32(data + 18) == 0 && get_u32(data + 22) == 0 && get_u32(data + 26) == 0);
    read_page(data, 0, 0);
    uint32_t total = get_u32(data + 10);
    for (unsigned i = 0; i < 100; ++i) {
        split_diagnostics_rpc_begin(0x55);
        split_diagnostics_rpc_end(0x55, false, NULL);
    }
    read_page(data, 0, 0);
    assert(get_u32(data + 10) == total + 1); // Failing retries cannot flood the ring.
    read_page(data, 1, 0);
    assert(get_u32(data + 6) == 101 && get_u32(data + 10) == 100);
    read_page(data, 2, 0);
    assert(memcmp(data + 10, reply, 4) == 0); // Last peer survives loss of link.
}

static void test_retention_wrap_and_read_only(void) {
    uint8_t data[32], copy[32];
    read_page(data, 0, 0);
    const uint32_t before = get_u32(data + 10);
    for (unsigned i = 0; i < 140; ++i)
        split_diagnostics_event(SPLIT_DIAG_DEVICE, i, 2, 0);
    read_page(data, 3, before + 1);
    assert(data[18] == 0); // Overwritten event is not silently replaced by another.
    read_page(data, 3, before + 13);
    assert(data[18] == 1 && data[14] == SPLIT_DIAG_DEVICE && data[15] == 12);
    memcpy(copy, data, 32);
    read_page(data, 3, before + 13);
    assert(memcmp(data, copy, 32) == 0);
    sample.device = 0; // Returning to USB must not clear history.
    split_diagnostics_loop();
    read_page(data, 3, before + 140);
    assert(data[18] == 1 && data[15] == 139);
    // Timer rollover is normal unsigned elapsed-time arithmetic.
    now = UINT32_MAX - 15;
    split_diagnostics_loop();
    now = 16;
    uint8_t reply[4];
    split_diagnostics_reply(reply);
    assert(reply[2] == 2 && reply[3] == 0);
    now += 10000;
    split_diagnostics_reply(reply);
    assert(reply[2] == UINT8_MAX && reply[3] == 0);
    assert(!lock_depth);
}

int main(void) {
    test_getter_boundary();
    test_peer_observes_stalled_main();
    test_retention_wrap_and_read_only();
    puts("Split diagnostics: stalled peer, read-only getters, ring retention and timer wrap passed");
}

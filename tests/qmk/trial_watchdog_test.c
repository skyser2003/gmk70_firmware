// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/trial_watchdog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t address;
    uint32_t value;
    uint32_t reads;
} reg_t;

static reg_t registers[] = {
    {0x40010028, 0, 0}, {0x4001002C, 0, 0}, {0x40010444, 0, 0}, {0x40010448, 0, 0}, {0x40010800, 0, 0}, {0x40010804, 0, 0}, {0x40010808, 0, 0}, {0x4001080C, 0, 0}, {0x40010C3C, 0, 0}, {0x40010C64, 0, 0}, {0xE0042004, 0, 0},
};
static struct {
    uint32_t address;
    uint32_t value;
} writes[32];
static unsigned write_count, start_count;
static uint32_t lsi_delay, initial_busy, update_delay, pending_pvu, pending_rvu;
static uint32_t ignored_write, corrupt_read, stuck_update_mask;
static bool     write_unlocked, last_access_write;
static uint32_t last_address, last_value;

static reg_t *reg_at(uint32_t address) {
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        if (registers[i].address == address) return &registers[i];
    }
    assert(!"access outside documented watchdog registers");
    return NULL;
}

static uint32_t decrement(uint32_t *remaining) {
    if (*remaining == 0) return 0;
    if (*remaining != UINT32_MAX) --*remaining;
    return 1;
}

static uint32_t read_register(uint32_t address) {
    reg_t *reg = reg_at(address);
    ++reg->reads;
    last_access_write = false;
    last_address      = address;
    if (address == 0x40010448) {
        assert(reg_at(0x40010028)->value == 0 && reg_at(0x4001002C)->value == 0);
        return reg_at(0x40010444)->value & 1U && !decrement(&lsi_delay) ? 1U : 0;
    }
    if (address == 0x4001080C) {
        if (decrement(&initial_busy)) return 3;
        return decrement(&pending_pvu) | (decrement(&pending_rvu) << 1);
    }
    if (address == 0x40010804) assert(pending_pvu == 0);
    if (address == 0x40010808) assert(pending_rvu == 0);
    return reg->value ^ (address == corrupt_read ? 1U : 0);
}

static void write_register(uint32_t address, uint32_t value) {
    reg_t *reg = reg_at(address);
    assert(write_count < sizeof(writes) / sizeof(writes[0]));
    writes[write_count].address = address;
    writes[write_count++].value = value;
    last_access_write           = true;
    last_address                = address;
    last_value                  = value;
    if (address == ignored_write) return;
    if (address == 0x40010444 && (reg_at(0x40010028)->value != 3 || reg_at(0x4001002C)->value != 12)) return;
    if (address == 0x40010804 || address == 0x40010808) {
        if (!write_unlocked) return;
        assert(initial_busy == 0);
        if (address == 0x40010804) pending_pvu = stuck_update_mask & 1U ? UINT32_MAX : update_delay;
        if (address == 0x40010808) pending_rvu = stuck_update_mask & 2U ? UINT32_MAX : update_delay;
    }
    if (address == 0x40010800) {
        write_unlocked = value == 0x5555;
        if (value == 0xCCCC) {
            ++start_count;
            assert((reg_at(0xE0042004)->value & 0x100) == 0);
            assert((reg_at(0x40010C3C)->value & 4) && (reg_at(0x40010C64)->value & 1));
            assert(lsi_delay == 0 && reg_at(0x40010448)->reads);
            assert(reg_at(0x40010804)->value == 3 && reg_at(0x40010804)->reads);
            assert(reg_at(0x40010808)->value == 2999 && reg_at(0x40010808)->reads);
            assert(reg_at(0x40010800)->value == 0xAAAA);
        }
    }
    reg->value = value;
}

static const gmk_trial_mmio_t io = {.read = read_register, .write = write_register};

static void fixture(void) {
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        registers[i].value = 0;
        registers[i].reads = 0;
    }
    memset(writes, 0, sizeof(writes));
    reg_at(0x40010C3C)->value = 0x7B;
    reg_at(0x40010C64)->value = 4;
    reg_at(0xE0042004)->value = 0x320;
    reg_at(0x40010808)->value = 0xFFF;
    write_count = start_count = 0;
    lsi_delay = initial_busy = update_delay = pending_pvu = pending_rvu = 0;
    ignored_write = corrupt_read = stuck_update_mask = 0;
    write_unlocked = last_access_write = false;
    last_address = last_value = 0;
}

static void assert_safe_failure(void) {
    assert(!gmk_trial_watchdog_start(&io));
    assert(start_count == 0);
    assert(reg_at(0x40010028)->value == 0 && reg_at(0x4001002C)->value == 0);
    assert(!write_unlocked);
    for (unsigned i = 0; i < write_count; ++i)
        assert(writes[i].address != 0x40010800 || writes[i].value != 0xCCCC);
}

// Omitting either clock gate, the analog unlock/relock, a busy wait, or the
// reload/start ordering must break this independently specified register trace.
static void test_delayed_ready_start_sequence(void) {
    fixture();
    lsi_delay    = 3;
    initial_busy = 2;
    update_delay = 4;
    assert(gmk_trial_watchdog_start(&io));
    static const uint32_t expected[][2] = {
        {0xE0042004, 0x220}, {0x40010C3C, 0x7F}, {0x40010C64, 5}, {0x40010028, 3}, {0x4001002C, 12}, {0x40010444, 1}, {0x40010028, 0}, {0x4001002C, 0}, {0x40010800, 0x5555}, {0x40010804, 3}, {0x40010808, 2999}, {0x40010800, 0xAAAA}, {0x40010800, 0xCCCC},
    };
    assert(write_count == sizeof(expected) / sizeof(expected[0]));
    for (unsigned i = 0; i < write_count; ++i) {
        assert(writes[i].address == expected[i][0]);
        assert(writes[i].value == expected[i][1]);
    }
    assert(reg_at(0x40010448)->reads == 4);
    assert(reg_at(0x4001080C)->reads == 8);
    assert(start_count == 1 && !write_unlocked);
    assert(last_access_write && last_address == 0x40010800 && last_value == 0xCCCC);
}

// Rejected MMIO writes must never permit trial execution, even if later status
// reads look ready. This catches unchecked gate, freeze, and LSI enable writes.
static void test_rejected_preparation_writes(void) {
    const uint32_t addresses[] = {0xE0042004, 0x40010C3C, 0x40010C64, 0x40010028, 0x4001002C, 0x40010444, 0x40010800};
    for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        fixture();
        ignored_write = addresses[i];
        assert_safe_failure();
    }
}

// Busy loops are finite, and no watchdog key is used if LSI never becomes ready
// or a previous configuration operation never finishes.
static void test_preparation_timeouts(void) {
    fixture();
    lsi_delay = UINT32_MAX;
    assert_safe_failure();
    assert(reg_at(0x40010448)->reads == GMK_TRIAL_WATCHDOG_POLL_LIMIT);
    assert(reg_at(0x40010800)->value == 0);

    fixture();
    initial_busy = UINT32_MAX;
    assert_safe_failure();
    assert(reg_at(0x4001080C)->reads == GMK_TRIAL_WATCHDOG_POLL_LIMIT);
    assert(reg_at(0x40010800)->value == 0);
}

// A lost configuration write or stuck update must relock PR/RLR rather than
// starting a watchdog with an unverified deadline.
static void test_configuration_failures(void) {
    const uint32_t addresses[] = {0x40010804, 0x40010808};
    for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        fixture();
        ignored_write = addresses[i];
        assert_safe_failure();
        assert(last_access_write && last_address == 0x40010800 && last_value == 0);

        fixture();
        corrupt_read = addresses[i];
        assert_safe_failure();
        assert(last_access_write && last_address == 0x40010800 && last_value == 0);
    }
    for (unsigned mask = 1; mask <= 3; ++mask) {
        fixture();
        stuck_update_mask = mask;
        assert_safe_failure();
        assert(reg_at(0x4001080C)->reads == GMK_TRIAL_WATCHDOG_POLL_LIMIT + 1);
        assert(last_access_write && last_address == 0x40010800 && last_value == 0);
    }
}

// Readiness on the final allowed poll is accepted at every wait, catching
// off-by-one budgets without depending on host clock speed.
static void test_final_poll_ready(void) {
    fixture();
    lsi_delay = GMK_TRIAL_WATCHDOG_POLL_LIMIT - 1;
    assert(gmk_trial_watchdog_start(&io));
    assert(reg_at(0x40010448)->reads == GMK_TRIAL_WATCHDOG_POLL_LIMIT);

    fixture();
    initial_busy = GMK_TRIAL_WATCHDOG_POLL_LIMIT - 1;
    assert(gmk_trial_watchdog_start(&io));
    assert(reg_at(0x4001080C)->reads == GMK_TRIAL_WATCHDOG_POLL_LIMIT + 1);

    fixture();
    update_delay = GMK_TRIAL_WATCHDOG_POLL_LIMIT - 1;
    assert(gmk_trial_watchdog_start(&io));
    assert(reg_at(0x4001080C)->reads == GMK_TRIAL_WATCHDOG_POLL_LIMIT + 1);
}

static void test_invalid_adapter(void) {
    fixture();
    assert(!gmk_trial_watchdog_start(NULL));
    const gmk_trial_mmio_t no_read  = {.read = NULL, .write = write_register};
    const gmk_trial_mmio_t no_write = {.read = read_register, .write = NULL};
    assert(!gmk_trial_watchdog_start(&no_read));
    assert(!gmk_trial_watchdog_start(&no_write));
    assert(write_count == 0);
}

int main(void) {
    test_delayed_ready_start_sequence();
    test_rejected_preparation_writes();
    test_preparation_timeouts();
    test_configuration_failures();
    test_final_poll_ready();
    test_invalid_adapter();
    puts("trial watchdog tests passed");
    return 0;
}

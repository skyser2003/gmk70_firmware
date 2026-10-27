// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/trial_role.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    uint32_t address;
    uint32_t value;
    uint32_t initial;
} reg_t;

static reg_t registers[] = {
    {0x40010C1C, 0, 0}, {0x40010C48, 0, 0}, {0x40000400, 0, 0}, {0x4000040C, 0, 0}, {0x40000410, 0, 0}, {0x40000430, 0, 0},
};
static struct {
    uint32_t address;
    uint32_t value;
} writes[32];
static unsigned write_count, gpio_reads, input_reads;
static uint32_t ignored_write;
static bool     unstable;

static reg_t *reg_at(uint32_t address) {
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i)
        if (registers[i].address == address) return &registers[i];
    assert(!"outside the vendor role sampler register set");
    return NULL;
}

static void require_gpio_clock(uint32_t address) {
    if (address < 0x40000400 || address > 0x40000430) return;
    // Missing either APB1 input or either peripheral gate reproduces the
    // pre-CRT bus-access defect, independently of sampler implementation.
    assert(reg_at(0x40010C1C)->value & 0x80);
    assert((reg_at(0x40010C48)->value & 0x8040) == 0x8040);
}

static uint32_t read_register(uint32_t address) {
    require_gpio_clock(address);
    if (address >= 0x40000400 && address <= 0x40000430) ++gpio_reads;
    if (address == 0x40000410 && unstable && ++input_reads == 2) return reg_at(address)->value ^ 0x200;
    return reg_at(address)->value;
}

static void write_register(uint32_t address, uint32_t value) {
    require_gpio_clock(address);
    assert(write_count < sizeof(writes) / sizeof(writes[0]));
    writes[write_count].address = address;
    writes[write_count++].value = value;
    if (address == ignored_write) return;
    reg_t *reg = reg_at(address);
    if (address == 0x40000400 || address == 0x4000040C) {
        // CFGMSK locks a pad's packed two-bit configuration fields.
        uint32_t frozen = reg_at(0x40000430)->value;
        for (unsigned pin = 0; pin < 16; ++pin) {
            uint32_t mask = 3U << (pin * 2);
            if (!(frozen & (1U << pin))) reg->value = (reg->value & ~mask) | (value & mask);
        }
    } else {
        reg->value = value;
    }
}

static const gmk_trial_mmio_t io = {.read = read_register, .write = write_register};

static void fixture(uint32_t pre, uint32_t clocks, bool left) {
    reg_at(0x40010C1C)->value = pre;
    reg_at(0x40010C48)->value = clocks;
    reg_at(0x40000400)->value = 0xA5A5A5A5;
    reg_at(0x4000040C)->value = 0x5A5A5A5A;
    reg_at(0x40000410)->value = left ? 0x200 : 0;
    reg_at(0x40000430)->value = 0x96A5;
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i)
        registers[i].initial = registers[i].value;
    write_count = gpio_reads = input_reads = 0;
    ignored_write                          = 0;
    unstable                               = false;
}

static void assert_restored(void) {
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i)
        assert(registers[i].value == registers[i].initial);
    assert(writes[write_count - 1].address == 0x40010C1C);
    assert(writes[write_count - 1].value == reg_at(0x40010C1C)->initial);
}

static void test_reset_clock_source_enabled_before_first_gpio_access(void) {
    for (unsigned left = 0; left < 2; ++left) {
        fixture(0, 0, left);
        uint32_t role = UINT32_MAX;
        assert(gmk_trial_role_read(&io, &role));
        assert(role == (left ? 0U : 1U));
        assert(writes[0].address == 0x40010C1C && writes[0].value == 0x80);
        assert(writes[1].address == 0x40010C48 && writes[1].value == 0x8040);
        assert(gpio_reads > 0);
        assert_restored();
    }
}

static void test_live_clock_and_unrelated_gpio_configuration_preserved(void) {
    fixture(0xA9, 0xF124, true);
    uint32_t role = UINT32_MAX;
    assert(gmk_trial_role_read(&io, &role) && role == 0);
    assert_restored();
}

static void test_failed_input_clock_never_accesses_gpio(void) {
    fixture(0x21, 0x1234, true);
    ignored_write = 0x40010C1C;
    uint32_t role = UINT32_MAX;
    assert(!gmk_trial_role_read(&io, &role));
    assert(role == UINT32_MAX && gpio_reads == 0);
    assert_restored();
}

static void test_failed_gates_and_configuration_restore_every_register(void) {
    const uint32_t ignored[] = {0x40010C48, 0x40000400, 0x4000040C};
    for (unsigned i = 0; i < sizeof(ignored) / sizeof(ignored[0]); ++i) {
        fixture(0x21, 0x1234, true);
        ignored_write = ignored[i];
        uint32_t role = UINT32_MAX;
        assert(!gmk_trial_role_read(&io, &role));
        assert(role == UINT32_MAX);
        assert_restored();
    }
}

static void test_unstable_input_is_rejected_after_restoration(void) {
    fixture(0, 0, true);
    unstable      = true;
    uint32_t role = UINT32_MAX;
    assert(!gmk_trial_role_read(&io, &role));
    assert(role == UINT32_MAX);
    assert_restored();
}

static void test_invalid_adapters_do_not_access_hardware(void) {
    fixture(0, 0, false);
    uint32_t               role     = UINT32_MAX;
    const gmk_trial_mmio_t no_read  = {.read = NULL, .write = write_register};
    const gmk_trial_mmio_t no_write = {.read = read_register, .write = NULL};
    assert(!gmk_trial_role_read(NULL, &role));
    assert(!gmk_trial_role_read(&no_read, &role));
    assert(!gmk_trial_role_read(&no_write, &role));
    assert(!gmk_trial_role_read(&io, NULL));
    assert(write_count == 0 && gpio_reads == 0);
}

int main(void) {
    test_reset_clock_source_enabled_before_first_gpio_access();
    test_live_clock_and_unrelated_gpio_configuration_preserved();
    test_failed_input_clock_never_accesses_gpio();
    test_failed_gates_and_configuration_restore_every_register();
    test_unstable_input_is_rejected_after_restoration();
    test_invalid_adapters_do_not_access_hardware();
    puts("trial role tests passed");
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../firmware/qmk-common/drivers/wireless/wb32_stop.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

struct test_register {
    uintptr_t address;
    uint32_t  value;
};
static struct test_register registers[] = {
    {0xe000e010U, 7U}, {0xe000ed04U, 0x123U}, {0x40010000U, 0xffffffffU}, {0x40010020U, 0U}, {0xe000ed10U, 0x10U},
};
static const struct test_register expected_writes[] = {
    {0xe000e010U, 6U}, {0xe000ed04U, 0x02000123U}, {0x40010000U, 9U}, {0x40010000U, 0x3b00dU}, {0x40010020U, 0x3b3U}, {0xe000ed10U, 0x14U}, {0xe000ed10U, 0x10U}, {0xe000e010U, 7U},
};
static unsigned writes, prepares, waits, resumes;
static uint32_t exti_pending, nvic_pending;

static uint32_t read32(uintptr_t address) {
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i)
        if (registers[i].address == address) return registers[i].value;
    assert(false && "STOP primitive must not read or clear unrelated registers");
    return 0;
}
static void write32(uint32_t value, uintptr_t address) {
    assert(writes < sizeof(expected_writes) / sizeof(expected_writes[0]));
    assert(expected_writes[writes].address == address);
    assert(expected_writes[writes].value == value);
    ++writes;
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        if (registers[i].address != address) continue;
        registers[i].value = value;
        return;
    }
    assert(false);
}
static void prepare(void) {
    assert(writes == 5 && prepares == 0 && waits == 0 && resumes == 0);
    assert(exti_pending == 0x400U && nvic_pending == 0x20U);
    ++prepares;
    /* A peer edge during the vendor preparation must remain pending for WFI. */
    exti_pending |= 0x80U;
    nvic_pending |= 0x40U;
}
static void wait_for_interrupt(void) {
    assert(writes == 6 && prepares == 1 && resumes == 0);
    assert(read32(0xe000ed10U) == 0x14U);
    assert(read32(0xe000e010U) == 6U);
    assert(exti_pending == 0x480U && nvic_pending == 0x60U);
    ++waits;
}
static void resume(void) {
    assert(writes == 6 && waits == 1 && resumes == 0);
    ++resumes;
}

/* Clearing pending IRQs, moving PRE/POST, or dropping DBP/FCLKSD breaks wake. */
int main(void) {
    const struct wb32_stop_ops ops = {read32, write32, prepare, wait_for_interrupt, resume};
    exti_pending                   = 0x400U;
    nvic_pending                   = 0x20U;
    wb32_stop_enter(&ops);
    assert(writes == 8 && prepares == 1 && waits == 1 && resumes == 1);
    assert(exti_pending == 0x480U && nvic_pending == 0x60U);
    puts("Shared WB32 STOP: exact MMIO order, vendor hooks and pending wake edges passed");
    return 0;
}

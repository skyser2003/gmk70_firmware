// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/trial_deadline.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t demcr, control, cycles;
static uint32_t ignored_write, counter_reads, writes, delay;
static bool     advancing;

static uint32_t read_register(uint32_t address) {
    if (address == 0xE000EDFC) return demcr;
    if (address == 0xE0001000) return control;
    assert(address == 0xE0001004);
    ++counter_reads;
    if (delay) {
        --delay;
    } else if (advancing && (demcr & (1U << 24)) && (control & 1U)) {
        ++cycles;
    }
    return cycles;
}

static void write_register(uint32_t address, uint32_t value) {
    assert(address == 0xE000EDFC || address == 0xE0001000);
    ++writes;
    if (address == ignored_write) return;
    if (address == 0xE000EDFC)
        demcr = value;
    else
        control = value;
}

static const gmk_trial_mmio_t io = {.read = read_register, .write = write_register};

static void fixture(void) {
    demcr         = 0x00000400;
    control       = 0x00000008;
    cycles        = 123;
    ignored_write = counter_reads = writes = delay = 0;
    advancing                                      = true;
}

// An incorrect cycle budget or signed comparison would expire early, miss the
// exact 12-second boundary, or fail when the free-running counter wraps.
static void test_twelve_second_deadline_and_wrap(void) {
    const uint32_t starts[] = {123, 0xFFFFFFF0};
    for (unsigned i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        fixture();
        cycles = starts[i];
        gmk_trial_deadline_t deadline;
        assert(gmk_trial_deadline_begin(&deadline, &io, 96000000));
        assert(demcr == 0x01000400 && control == 9);
        advancing      = false;
        uint32_t start = cycles;
        cycles         = start + 1151999999U;
        assert(!gmk_trial_deadline_expired(&deadline, &io));
        cycles = start + 1152000000U;
        assert(gmk_trial_deadline_expired(&deadline, &io));
        assert(writes == 2); // CYCCNT remains free-running and is never reset.
    }
}

// The pre-acknowledgment enable check must have a finite bound and preserve the
// original debug state when no counter exists, a write is lost, or it stalls.
static void test_enable_failures_restore_debug_state(void) {
    for (unsigned failure = 0; failure < 4; ++failure) {
        fixture();
        if (failure == 0) control |= 1U << 25;
        if (failure == 1) ignored_write = 0xE000EDFC;
        if (failure == 2) ignored_write = 0xE0001000;
        if (failure == 3) advancing = false;
        uint32_t             saved_demcr = demcr, saved_control = control;
        gmk_trial_deadline_t deadline;
        memset(&deadline, 0xA5, sizeof(deadline));
        assert(!gmk_trial_deadline_begin(&deadline, &io, 96000000));
        assert(demcr == saved_demcr && control == saved_control);
        assert(counter_reads <= 1025);
        assert(gmk_trial_deadline_expired(&deadline, &io));
    }
}

// Enabling the last allowed poll is sufficient; a counter that advances only
// after that budget must fail rather than spin indefinitely.
static void test_enable_poll_boundary(void) {
    fixture();
    delay = 1024;
    gmk_trial_deadline_t deadline;
    assert(gmk_trial_deadline_begin(&deadline, &io, 96000000));
    assert(counter_reads == 1025);

    fixture();
    delay = 1025;
    assert(!gmk_trial_deadline_begin(&deadline, &io, 96000000));
    assert(counter_reads == 1025);
    assert(demcr == 0x400 && control == 8);
}

// A counter which stops after the acknowledgment must force the fallback on
// the 1024th equal read. Progress restarts the consecutive-stall budget.
static void test_counter_stall_is_bounded_and_progress_resets_it(void) {
    fixture();
    gmk_trial_deadline_t deadline;
    assert(gmk_trial_deadline_begin(&deadline, &io, 96000000));
    advancing = false;
    for (unsigned i = 0; i < 1023; ++i)
        assert(!gmk_trial_deadline_expired(&deadline, &io));
    ++cycles;
    assert(!gmk_trial_deadline_expired(&deadline, &io));
    for (unsigned i = 0; i < 1023; ++i)
        assert(!gmk_trial_deadline_expired(&deadline, &io));
    assert(gmk_trial_deadline_expired(&deadline, &io));
}

// Setup and acknowledgment time must not shorten the execution deadline, and
// a stale stall count must not carry over across a rebase.
static void test_rebase_starts_a_fresh_deadline(void) {
    fixture();
    gmk_trial_deadline_t deadline;
    assert(gmk_trial_deadline_begin(&deadline, &io, 96000000));
    advancing = false;
    for (unsigned i = 0; i < 1023; ++i)
        assert(!gmk_trial_deadline_expired(&deadline, &io));
    cycles = 0xFFFFFFFE;
    gmk_trial_deadline_rebase(&deadline, &io);
    for (unsigned i = 0; i < 1023; ++i)
        assert(!gmk_trial_deadline_expired(&deadline, &io));
    cycles = 1151999997U;
    assert(!gmk_trial_deadline_expired(&deadline, &io));
    cycles = 1151999998U;
    assert(gmk_trial_deadline_expired(&deadline, &io));
}

// Multiplication must not overflow or admit deadlines of half a wrap or more.
static void test_clock_validation(void) {
    const uint32_t invalid_clocks[] = {0, 178956971, 357913942, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid_clocks) / sizeof(invalid_clocks[0]); ++i) {
        fixture();
        gmk_trial_deadline_t deadline;
        assert(!gmk_trial_deadline_begin(&deadline, &io, invalid_clocks[i]));
        assert(writes == 0 && counter_reads == 0);
        assert(gmk_trial_deadline_expired(&deadline, &io));
    }
    const uint32_t clocks[]  = {1, 178956970};
    const uint32_t budgets[] = {12, 2147483640};
    for (unsigned i = 0; i < sizeof(clocks) / sizeof(clocks[0]); ++i) {
        fixture();
        gmk_trial_deadline_t deadline;
        assert(gmk_trial_deadline_begin(&deadline, &io, clocks[i]));
        advancing      = false;
        uint32_t start = cycles;
        cycles         = start + budgets[i] - 1;
        assert(!gmk_trial_deadline_expired(&deadline, &io));
        cycles = start + budgets[i];
        assert(gmk_trial_deadline_expired(&deadline, &io));
    }
}

static void test_invalid_adapter_fails_closed(void) {
    fixture();
    const gmk_trial_mmio_t no_read  = {.read = NULL, .write = write_register};
    const gmk_trial_mmio_t no_write = {.read = read_register, .write = NULL};
    gmk_trial_deadline_t   deadline;
    assert(!gmk_trial_deadline_begin(NULL, &io, 96000000));
    assert(!gmk_trial_deadline_begin(&deadline, NULL, 96000000));
    assert(!gmk_trial_deadline_begin(&deadline, &no_read, 96000000));
    assert(!gmk_trial_deadline_begin(&deadline, &no_write, 96000000));
    assert(writes == 0 && counter_reads == 0);
    assert(gmk_trial_deadline_expired(NULL, &io));
    assert(gmk_trial_deadline_expired(&deadline, &io));
    gmk_trial_deadline_rebase(NULL, &io);
    assert(gmk_trial_deadline_begin(&deadline, &io, 96000000));
    assert(gmk_trial_deadline_expired(&deadline, NULL));
    assert(gmk_trial_deadline_expired(&deadline, &no_read));
    gmk_trial_deadline_rebase(&deadline, NULL);
    assert(gmk_trial_deadline_expired(&deadline, &io));
}

int main(void) {
    test_twelve_second_deadline_and_wrap();
    test_enable_failures_restore_debug_state();
    test_enable_poll_boundary();
    test_counter_stall_is_bounded_and_progress_resets_it();
    test_rebase_starts_a_fresh_deadline();
    test_clock_validation();
    test_invalid_adapter_fails_closed();
    puts("trial deadline tests passed");
    return 0;
}

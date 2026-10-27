// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/trial_platform.h"
#include "update/trial_watchdog.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

uint32_t        SystemCoreClock = 96000000;
static uint8_t  commit[GMK_PAGE_SIZE];
static uint32_t demcr, control, cycles, reset_flags;
static unsigned irq_disables, watchdog_starts, resets;
static bool     stalled, fail_watchdog;
static jmp_buf  reset_jump;

const uint8_t *gmk_flash_view(uint32_t address) {
    assert(address == GMK_COMMIT_BASE);
    return commit;
}

bool gmk_flash_page(uint32_t address, const uint8_t data[GMK_PAGE_SIZE]) {
    assert(!"Runtime watchdog qualification must never write flash");
    return false;
}

uint32_t gmk_trial_runtime_read(uint32_t address) {
    switch (address) {
        case 0xE000EDFC:
            return demcr;
        case 0xE0001000:
            return control;
        case 0xE0001004:
            if (!stalled && (demcr & (1U << 24)) && (control & 1)) cycles += 1000000;
            return cycles;
        case 0x40010D00:
            return reset_flags;
        default:
            assert(false);
            return 0;
    }
}

void gmk_trial_runtime_write(uint32_t address, uint32_t value) {
    switch (address) {
        case 0xE000EDFC:
            demcr = value;
            break;
        case 0xE0001000:
            control = value;
            break;
        case 0x40010CC8:
            assert(value == 1);
            reset_flags = 0;
            break;
        case 0xE000ED0C:
            assert(value == 0x05FA0004 && irq_disables);
            ++resets;
            reset_flags |= GMK_TRIAL_BOOT_RESET_SOFTWARE;
            longjmp(reset_jump, 1);
        default:
            assert(false);
    }
}

void gmk_trial_runtime_disable_irq(void) {
    ++irq_disables;
}
void gmk_trial_runtime_barrier(void) {}

bool gmk_trial_watchdog_start(const gmk_trial_mmio_t *io) {
    assert(irq_disables && io && !reset_flags);
    gmk_trial_boot_result_t report;
    assert(gmk_trial_boot_report(&gmk_trial_record, &report));
    assert(report.action == GMK_TRIAL_BOOT_PROBE && report.phase == GMK_TRIAL_BOOT_ENTERED);
    ++watchdog_starts;
    return !fail_watchdog;
}

static void fixture(void) {
    memset((void *)&gmk_trial_record, 0, sizeof(gmk_trial_record));
    memset(commit, 0xFF, sizeof(commit));
    demcr = control = cycles = 0;
    reset_flags              = GMK_TRIAL_BOOT_RESET_POWER;
    irq_disables = watchdog_starts = resets = 0;
    stalled = fail_watchdog = false;
    gmk_trial_runtime_init();
    assert(!reset_flags && !irq_disables && !watchdog_starts && !demcr && !control);
}

static void execute(void) {
    if (!setjmp(reset_jump)) {
        gmk_trial_platform_reset();
        assert(false);
    }
    assert(resets == 1 && reset_flags == GMK_TRIAL_BOOT_RESET_SOFTWARE);
    gmk_trial_runtime_init();
    uint8_t result[16];
    gmk_trial_platform_result(result);
    assert(gmk_read_u32(result) == 0x302);
    assert(gmk_read_u32(result + 4) == GMK_TRIAL_BOOT_RESET_SOFTWARE);
    assert(!gmk_read_u32(result + 8) && !gmk_read_u32(result + 12));
}

int main(void) {
    fixture();
    assert(!gmk_trial_platform_prepare(GMK_TRIAL_BOOT_RUN, (gmk_image_t){128, 1}, 0));
    assert(!gmk_trial_platform_prepare(GMK_TRIAL_BOOT_PROBE, (gmk_image_t){0, 1}, 0));
    assert(!gmk_trial_platform_prepare(GMK_TRIAL_BOOT_PROBE, (gmk_image_t){0}, 2));
    assert(!gmk_trial_platform_prepare(GMK_TRIAL_BOOT_PROBE, (gmk_image_t){0}, 1));
    assert(!demcr && !control && !watchdog_starts);
    commit[0] = 0;
    assert(!gmk_trial_platform_prepare(GMK_TRIAL_BOOT_PROBE, (gmk_image_t){0}, 0));
    fixture();
    stalled = true;
    assert(!gmk_trial_platform_prepare(GMK_TRIAL_BOOT_PROBE, (gmk_image_t){0}, 0));
    assert(!demcr && !control && !irq_disables);
    // A started IWDG that fails to expire still reaches software reset.
    fixture();
    assert(gmk_trial_platform_prepare(GMK_TRIAL_BOOT_PROBE, (gmk_image_t){0}, 0));
    execute();
    assert(watchdog_starts == 1 && cycles >= 1152000000);
    // Explicit fallback proof must skip IWDG start but use the same deadline.
    fixture();
    gmk_trial_runtime_fallback_test(true);
    assert(gmk_trial_platform_prepare(GMK_TRIAL_BOOT_PROBE, (gmk_image_t){0}, 0));
    execute();
    assert(!watchdog_starts && cycles >= 1152000000);
    // A stopped counter after ACK also reaches software reset.
    fixture();
    assert(gmk_trial_platform_prepare(GMK_TRIAL_BOOT_PROBE, (gmk_image_t){0}, 0));
    stalled = true;
    execute();
    assert(watchdog_starts == 1 && cycles < 1152000000);
    fixture();
    fail_watchdog = true;
    assert(gmk_trial_platform_prepare(GMK_TRIAL_BOOT_PROBE, (gmk_image_t){0}, 0));
    execute();
    assert(watchdog_starts == 1 && cycles < 1152000000);
    puts("Runtime probe adapter: normal initialization, PROBE-only admission and bounded fallback passed");
}

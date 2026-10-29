/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "image.h"
#include "wb32_watchdog.h"
#include <wb32_flash_lowlevel.h>
#include <zephyr/sys/sys_io.h>

#define MMIO_BASE 0x40010000U
#define FMC 0x40017800U
#define REG(address) registers[((address) - MMIO_BASE) / 4]
#define KR 0x40010800U
#define SR 0x4001080cU
#define FHSI 0x40010438U
#define PCLK 0x40010c60U
static uint32_t registers[0x8000 / 4];
static uint32_t debug_cr, expected_fhsi;
static unsigned reads, writes, primask, operations, preops, fail_operation, keys;
static unsigned flash_unlock;
static uint32_t key_log[16];
static bool     corrupt, stuck_lsi, stuck_sr_before, stuck_sr_after, dropped_pr, dropped_clock;

uint32_t sys_read32(uintptr_t address) {
    assert(++reads < 3000100);
    if (address == 0xe0042004U) return debug_cr;
    assert(address >= MMIO_BASE && address < MMIO_BASE + sizeof(registers));
    assert(address % 4 == 0);
    if (address == 0x40010448U) return stuck_lsi ? 0 : 1;
    if (address == SR && (stuck_sr_before || (stuck_sr_after && keys))) return 3;
    return REG(address);
}

void sys_write32(uint32_t value, uintptr_t address) {
    ++writes;
    if (address == 0xe0042004U) {
        debug_cr = value;
        return;
    }
    assert(address >= MMIO_BASE && address < MMIO_BASE + sizeof(registers));
    assert(address % 4 == 0);
    if (address == KR) {
        assert(keys < 16);
        key_log[keys++] = value;
    }
    if ((dropped_pr && address == 0x40010804U) || (dropped_clock && address == 0x40010c64U)) return;
    if (address == FMC + 0xc) {
        if (value == 0x5188da08U) {
            assert(flash_unlock == 0);
            flash_unlock = 1;
        } else if (value == 0x12586590U) {
            assert(flash_unlock == 1);
            flash_unlock = 2;
        } else {
            assert(value == 0);
            flash_unlock = 0;
        }
    }
    REG(address) = value;
}

uint32_t gmk_wb32_irq_lock(void) {
    uint32_t previous = primask;
    primask           = 1;
    return previous;
}

void gmk_wb32_irq_restore(uint32_t state) {
    primask = state;
}

void gmk_wb32_flash_preop(void) {
    assert(primask == 1);
    ++preops;
    /* Side effects of the exact pinned vendor PRE_OP blob, excluding analog
     * calibration and RNG sampling. Real adapter must restore these gates. */
    REG(FHSI) = 1;
    REG(0x40010c20U) |= 0x80;
    REG(0x40010c4cU) |= 0xa00;
    REG(0x40010c5cU) = 1;
    REG(0x40010028U) = 3;
    REG(0x4001002cU) = 12;
}

void gmk_wb32_flash_ram_operation(void) {
    assert(primask == 1);
    assert(REG(PCLK) == 1);
    assert(REG(FMC + 0xc) == 0x12586590U);
    assert((REG(FMC) & ~0x1fU) == 0x7f5f0d40U);
    assert(flash_unlock == 2);
    flash_unlock = 0;
    ++operations;
    REG(FMC + 8) = fail_operation == operations ? 4 : 0;
    if (REG(FMC + 8)) return;
    uint32_t address = REG(FMC + 0x10);
    assert((address >= GMK_STAGE_BASE && address <= GMK_COMMIT_BASE)
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
           || (address >= 0x0801c000U && address < 0x08020000U)
#endif
    );
    assert(address % GMK_PAGE_SIZE == 0);
    switch (REG(FMC) & 0x1fU) {
        case 8:
            memset((void *)(uintptr_t)address, 0xff, GMK_PAGE_SIZE);
            break;
        case 4:
            memset(&REG(FMC + 0x100), 0, GMK_PAGE_SIZE);
            break;
        case 12:
            memcpy((void *)(uintptr_t)address, &REG(FMC + 0x100), GMK_PAGE_SIZE);
            if (corrupt) *(uint8_t *)(uintptr_t)address ^= 1;
            break;
        default:
            assert(false);
    }
}

static void reset(void) {
    memset(registers, 0, sizeof(registers));
    flash_unlock = 0;
    reads = writes = primask = operations = preops = fail_operation = keys = 0;
    corrupt = stuck_lsi = stuck_sr_before = stuck_sr_after = dropped_pr = dropped_clock = false;
    debug_cr                                                                            = 0x301;
    expected_fhsi                                                                       = 0;
    REG(FHSI)                                                                           = 0;
    REG(PCLK)                                                                           = 4;
    REG(0x40010c20U)                                                                    = 0x12;
    REG(0x40010c4cU)                                                                    = 0x82;
    REG(0x40010c5cU)                                                                    = 4;
    REG(0x40010c3cU)                                                                    = 0x10;
    REG(0x40010c64U)                                                                    = 0x10;
    REG(0x40010444U)                                                                    = 0x20;
}

static void assert_restored(unsigned irq) {
    assert(primask == irq);
    assert(REG(FHSI) == expected_fhsi);
    assert(REG(PCLK) == 4);
    assert(REG(0x40010c20U) == 0x12);
    assert(REG(0x40010c4cU) == 0x82);
    assert(REG(0x40010c5cU) == 4);
    assert(REG(0x40010028U) == 0 && REG(0x4001002cU) == 0);
    assert(REG(FMC + 0xc) == 0);
    assert(REG(FMC) == 0x005f0000U);
}

static void flash_tests(void) {
    uint8_t page[GMK_PAGE_SIZE];
    for (unsigned i = 0; i < sizeof(page); ++i)
        page[i] = i ^ 0x5a;
    const uint32_t rejected[] = {0, GMK_FLASH_BASE, GMK_APP_BASE, GMK_APP_BASE + GMK_APP_SIZE - GMK_PAGE_SIZE, GMK_STAGE_BASE - GMK_PAGE_SIZE, GMK_STAGE_BASE + 1, GMK_STAGE_BASE + GMK_APP_SIZE, GMK_COMMIT_BASE - GMK_PAGE_SIZE, GMK_COMMIT_BASE + 1, GMK_COMMIT_BASE + GMK_PAGE_SIZE, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        reset();
        assert(!gmk_flash_page(rejected[i], page));
        assert(reads == 0 && writes == 0 && operations == 0 && primask == 0);
    }
    reset();
    assert(!gmk_flash_page(GMK_STAGE_BASE, NULL));
    assert(reads == 0 && writes == 0);
    assert(gmk_flash_view(GMK_STAGE_BASE) == (void *)(uintptr_t)GMK_STAGE_BASE);
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    reset();
    assert(!gmk_flash_page(GMK_STAGE_BASE, page));
    assert(!gmk_flash_page(GMK_COMMIT_BASE, page));
    assert(reads == 0 && writes == 0 && operations == 0);
    memset(page, 0xff, sizeof(page));
    assert(gmk_flash_page(GMK_COMMIT_BASE, page));
    assert_restored(0);
#else
    const uint32_t accepted[] = {GMK_STAGE_BASE, GMK_STAGE_BASE + GMK_APP_SIZE - GMK_PAGE_SIZE, GMK_COMMIT_BASE};
    for (unsigned i = 0; i < sizeof(accepted) / sizeof(accepted[0]); ++i) {
        reset();
        primask       = i % 2;
        expected_fhsi = REG(FHSI) = i % 2;
        assert(gmk_flash_page(accepted[i], page));
        assert(memcmp(gmk_flash_view(accepted[i]), page, sizeof(page)) == 0);
        assert(operations == 3 && preops == 2);
        assert_restored(i % 2);
    }
    for (unsigned failure = 1; failure <= 3; ++failure) {
        reset();
        fail_operation = failure;
        assert(!gmk_flash_page(GMK_STAGE_BASE, page));
        assert(operations == failure);
        assert_restored(0);
    }
    reset();
    memcpy((void *)(uintptr_t)GMK_STAGE_BASE, page, sizeof(page));
    assert(gmk_flash_page(GMK_STAGE_BASE, gmk_flash_view(GMK_STAGE_BASE)));
    assert(memcmp(gmk_flash_view(GMK_STAGE_BASE), page, sizeof(page)) == 0);
    assert_restored(0);
    reset();
    corrupt = true;
    assert(!gmk_flash_page(GMK_STAGE_BASE, page));
    assert_restored(0);
#endif
}

#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
extern bool gmk_zmk_settings_flash_page(uint32_t, const uint8_t[256]);
static void settings_bounds(void) {
    uint8_t page[256];
    memset(page, 0x53, sizeof(page));
    const uint32_t bad[] = {0, GMK_FLASH_BASE, GMK_APP_BASE, 0x0801bf00, 0x0801c001, GMK_STAGE_BASE, GMK_COMMIT_BASE, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        reset();
        assert(!gmk_zmk_settings_flash_page(bad[i], page));
        assert(!writes && !reads && !operations);
    }
    assert(!gmk_zmk_settings_flash_page(0x0801c000, NULL));
    for (uint32_t a = 0x0801c000; a < 0x08020000; a += 256) {
        reset();
        assert(gmk_zmk_settings_flash_page(a, page));
        assert(memcmp(gmk_flash_view(a), page, 256) == 0);
        assert_restored(0);
    }
}
#endif
static void watchdog_tests(void) {
    reset();
    gmk_zmk_watchdog_feed();
    assert(writes == 0);
#if defined(CONFIG_GMK70_STAGING_QUALIFICATION) || defined(CONFIG_GMK70_WATCHDOG_ROLLBACK)
    assert(!gmk_zmk_watchdog_init());
    gmk_zmk_watchdog_feed();
    assert(reads == 0 && writes == 0);
#else
    /* Model the short watchdog inherited from the trial launcher. It must
     * be reconfigured regardless of the reset-cause flags. */
    REG(0x40010804U) = 3;
    REG(0x40010808U) = 2999;
    assert(gmk_zmk_watchdog_init());
    assert(REG(0x40010804U) == 6 && REG(0x40010808U) == 4095);
    assert((REG(0x40010c3cU) & 4) && (REG(0x40010c64U) & 1));
    assert(REG(0x40010444U) == 0x21 && debug_cr == 0x201);
    assert(REG(0x40010c3cU) == 0x14 && REG(0x40010c64U) == 0x11);
    assert(keys == 3 && key_log[0] == 0x5555 && key_log[1] == 0xaaaa && key_log[2] == 0xcccc);
    unsigned before = writes;
    gmk_zmk_watchdog_feed();
    assert(writes == before + 1 && keys == 4 && key_log[3] == 0xaaaa);
    assert(gmk_zmk_watchdog_init());
    assert(keys == 7 && key_log[4] == 0x5555 && key_log[5] == 0xaaaa && key_log[6] == 0xcccc);
    /* Repeat initialization also handles a running watchdog without relying
     * on a reset-cause bit. Every readiness/configuration error is finite. */
    for (unsigned failure = 0; failure < 5; ++failure) {
        reset();
        stuck_lsi       = failure == 0;
        stuck_sr_before = failure == 1;
        stuck_sr_after  = failure == 2;
        dropped_pr      = failure == 3;
        dropped_clock   = failure == 4;
        assert(!gmk_zmk_watchdog_init());
        for (unsigned i = 0; i < keys; ++i)
            assert(key_log[i] != 0xaaaa && key_log[i] != 0xcccc);
        before = writes;
        gmk_zmk_watchdog_feed();
        assert(writes == before);
    }
#endif
}

int main(int argc, char **argv) {
    void *flash = mmap((void *)GMK_FLASH_BASE, 0x40000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(flash == (void *)GMK_FLASH_BASE);
    if (argc == 1 || strcmp(argv[1], "flash") == 0) {
        flash_tests();
        assert(gmk_running_size() == 0x304);
    }
    if (argc == 1 || strcmp(argv[1], "watchdog") == 0) watchdog_tests();

#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    settings_bounds();
#endif
    assert(munmap(flash, 0x40000) == 0);
    puts("WB32 update platform tests passed");
    return 0;
}

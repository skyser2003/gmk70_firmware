/*
 * Copyright (C) 2021...2022 Westberry Technology (ChangZhou) Corp., Ltd
 * Copyright (c) 2026 GMK70 contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Exact-page erase/latch/program sequence adapted from the pinned vendor
 * WB32FQ95xx hal_efl_lld.c; see firmware/zmk/PROVENANCE.md.
 */
#include "image.h"
#include <string.h>
#include <zephyr/sys/sys_io.h>
#include <wb32_flash_lowlevel.h>

#define FMC 0x40017800U
#define FHSI 0x40010438U
#define RCC 0x40010c00U
#define ANAKEY1 0x40010028U
#define ANAKEY2 0x4001002cU

const uint8_t *gmk_flash_view(uint32_t address) {
    return (const uint8_t *)(uintptr_t)address;
}

uint32_t gmk_running_size(void) {
    extern const uint8_t __gmk_image_end;
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    const uintptr_t base = GMK_STAGE_BASE;
#else
    const uintptr_t base = GMK_APP_BASE;
#endif
    uintptr_t end = (uintptr_t)&__gmk_image_end;
    if (end < base || end - base > GMK_APP_SIZE) return 0;
    return ((uint32_t)(end - base) + 3U) & ~3U;
}

static void set_fhsi(uint32_t value) {
    sys_write32(3, ANAKEY1);
    sys_write32(12, ANAKEY2);
    sys_write32(value, FHSI);
    sys_write32(0, ANAKEY1);
    sys_write32(0, ANAKEY2);
}

static bool operation(uint32_t op) {
    sys_write32(1, RCC + 0x60);
    sys_write32(0x7f5f0d40U | op, FMC);
    sys_write32(0x5188da08U, FMC + 0x0c);
    sys_write32(0x12586590U, FMC + 0x0c);
    gmk_wb32_flash_ram_operation();
    sys_write32(0, RCC + 0x60);
    sys_write32(0x005f0000U, FMC);
    return (sys_read32(FMC + 0x08) & 4U) == 0;
}

static bool addressed_operation(uint32_t address, uint32_t op) {
    gmk_wb32_flash_preop();
    sys_write32(address, FMC + 0x10);
    return operation(op);
}

bool gmk_flash_page(uint32_t address, const uint8_t data[GMK_PAGE_SIZE]) {
    bool staging = address >= GMK_STAGE_BASE && address < GMK_STAGE_BASE + GMK_APP_SIZE;
    if (!data || address % GMK_PAGE_SIZE || (!staging && address != GMK_COMMIT_BASE)) return false;
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    /* This image executes in staging. It may only invalidate a pending
     * manifest, never overwrite its own pages or create a durable commit. */
    if (address != GMK_COMMIT_BASE) return false;
    for (unsigned i = 0; i < GMK_PAGE_SIZE; ++i) {
        if (data[i] != 0xff) return false;
    }
#endif
    /* Snapshot first: the caller may supply flash memory, including the page
     * being replaced, and the FMC latch accepts aligned 32-bit words only. */
    uint32_t page[GMK_PAGE_SIZE / sizeof(uint32_t)];
    memcpy(page, data, sizeof(page));
    uint32_t irq     = gmk_wb32_irq_lock();
    uint32_t fhsi    = sys_read32(FHSI);
    uint32_t pclk    = sys_read32(RCC + 0x60);
    uint32_t apb2pre = sys_read32(RCC + 0x20);
    uint32_t apb2enr = sys_read32(RCC + 0x4c);
    uint32_t rngclk  = sys_read32(RCC + 0x5c);
    set_fhsi(1);
    bool ok = addressed_operation(address, 8) && operation(4);
    if (ok) {
        for (unsigned i = 0; i < GMK_PAGE_SIZE / sizeof(uint32_t); ++i) {
            sys_write32(page[i], FMC + 0x100 + i * sizeof(uint32_t));
        }
        ok = addressed_operation(address, 12);
    }
    /* Lock FMC on every path before restoring all clock gates changed by
     * PRE_OP or the operation helper, and finally the caller's IRQ mask. */
    sys_write32(0, FMC + 0x0c);
    sys_write32(0, FMC + 0x0c);
    sys_write32(pclk, RCC + 0x60);
    sys_write32(rngclk, RCC + 0x5c);
    sys_write32(apb2enr, RCC + 0x4c);
    sys_write32(apb2pre, RCC + 0x20);
    set_fhsi(fhsi);
    ok = ok && memcmp(gmk_flash_view(address), page, sizeof(page)) == 0;
    gmk_wb32_irq_restore(irq);
    return ok;
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "image.h"
#include "hal.h"
#include <string.h>

#ifndef GMK70_BOOT_STAGE
uint32_t gmk_running_size(void) {
    extern const uint8_t __flash0_free__;
    return (uintptr_t)&__flash0_free__ - GMK_APP_BASE;
}
#endif

const uint8_t *gmk_flash_view(uint32_t address) {
    return (const uint8_t *)(uintptr_t)address;
}

bool gmk_flash_page(uint32_t address, const uint8_t data[GMK_PAGE_SIZE]) {
#ifdef GMK70_BOOT_STAGE
    bool allowed = address >= GMK_APP_BASE && address < GMK_APP_BASE + GMK_APP_SIZE;
#else
    bool allowed = address >= GMK_STAGE_BASE && address < GMK_STAGE_BASE + GMK_APP_SIZE;
#endif
    if ((!allowed && address != GMK_COMMIT_BASE) || address % GMK_PAGE_SIZE) return false;
    // Use the pinned Westberry driver, including its RAM-resident flash operation
    // and vendor pre-operation sequence. No changes to option bytes or protection.
    uint32_t     previous_fhsi = ANCTL->FHSIENR;
    EFlashDriver driver        = {.state = FLASH_READY, .flash = FMC};
    efl_lld_start(&driver);
    flash_error_t result = efl_lld_program(&driver, address - GMK_FLASH_BASE, GMK_PAGE_SIZE, data);
    efl_lld_stop(&driver);
    PWR->ANAKEY1   = 0x03;
    PWR->ANAKEY2   = 0x0C;
    ANCTL->FHSIENR = previous_fhsi;
    PWR->ANAKEY1   = 0;
    PWR->ANAKEY2   = 0;
    return result == FLASH_NO_ERROR && memcmp(gmk_flash_view(address), data, GMK_PAGE_SIZE) == 0;
}

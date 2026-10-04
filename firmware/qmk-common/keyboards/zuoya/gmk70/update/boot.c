// SPDX-License-Identifier: GPL-2.0-or-later
// Resident installer: no scheduler, mutable globals, QMK calls, or application
// dependencies. Its whole ELF must fit below GMK_APP_BASE and has no data/BSS.
#include "image.h"
#include "hal.h"

__attribute__((naked, noreturn)) static void jump_vector(uint32_t address) {
    __asm__ volatile("ldr r1, [r0, #4]\n"
                     "ldr r0, [r0]\n"
                     "msr msp, r0\n"
                     "bx r1\n");
}

__attribute__((noreturn)) static void recovery(void) {
    SCB->VTOR = WB32_BOOTLOADER_ADDRESS;
    __set_CONTROL(0);
    __DSB();
    __ISB();
    __enable_irq();
    jump_vector(WB32_BOOTLOADER_ADDRESS);
}

__attribute__((noreturn)) static void fault_recovery(void) {
    // A direct branch from HardFault/NMI would leave IPSR in exception mode and
    // prevent the ROM's USB interrupts from running. Reset into thread mode.
    *(volatile uint32_t *)(GMK_RAM_END - 4) = 0xDEADBEEF;
    NVIC_SystemReset();
}

__attribute__((noreturn)) void gmk_boot_reset(void) {
    __disable_irq();
    volatile uint32_t *magic = (volatile uint32_t *)(GMK_RAM_END - 4);
    if (*magic == 0xDEADBEEF) {
        *magic = 0;
        recovery();
    }
    if (!gmk_install_pending() || !gmk_image_vectors(gmk_flash_view(GMK_APP_BASE), GMK_APP_SIZE)) recovery();
    SCB->VTOR = GMK_APP_BASE;
    __DSB();
    __ISB();
    jump_vector(GMK_APP_BASE);
}

__attribute__((used, section(".boot_vectors"))) const uintptr_t gmk_boot_vectors[16] = {
    GMK_RAM_END - 8, (uintptr_t)gmk_boot_reset, (uintptr_t)fault_recovery, (uintptr_t)fault_recovery, (uintptr_t)fault_recovery, (uintptr_t)fault_recovery, (uintptr_t)fault_recovery, 0, 0, 0, 0, (uintptr_t)fault_recovery, (uintptr_t)fault_recovery, 0, (uintptr_t)fault_recovery, (uintptr_t)fault_recovery,
};

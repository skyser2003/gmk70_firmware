/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "resident_handoff.h"
#include "image.h"
#include <string.h>
#include <cmsis_core.h>
#include <soc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/usb/usb_device.h>

#define RCC 0x40010c00U
#define IWDG 0x40010800U
/* Pinned Westberry wb32fq95xx.h: DMAC1 is on APB1, DMAC2 on APB2. */
#define DMAC1 0x40007c00U
#define DMAC2 0x4000fc00U

__attribute__((noreturn)) void gmk_zmk_resident_branch(uint32_t vectors);

bool gmk_zmk_resident_handoff(void) {
    /* Unlike a system reset this keeps the long IWDG configuration and PLL.
     * Only newly marked native receivers promise to normalize that state.
     * The existing image CRC covers the marker; it is not an authenticity key. */
    static const uint8_t marker[16] = {'G', 'M', 'K', '7', '0', 'Z', 'M', 'K', 'B', 'O', 'O', 'T', 1, 0, 0, 0};
    gmk_image_t          image;
    if (__get_IPSR() || (__get_CONTROL() & 1U) || sys_read32(GMK_RAM_END - 4) == 0xdeadbeefU || gmk_crc32(gmk_flash_view(GMK_FLASH_BASE), GMK_APP_BASE - GMK_FLASH_BASE) != 1770733249U || !gmk_manifest_decode(gmk_flash_view(GMK_COMMIT_BASE), &image) || !gmk_image_valid(GMK_STAGE_BASE, image) || memcmp(gmk_flash_view(GMK_STAGE_BASE) + image.size - sizeof(marker), marker, sizeof(marker))) return false;
    /* Admission requires the native port's verified 96 MHz clock setup and
     * fully settled longest watchdog period; feed-call telemetry is not proof. */
    if (sys_read32(RCC + 0x08) != 2 || sys_read32(RCC) != 0x21 || sys_read32(RCC + 4) != 1 || sys_read32(0x40010474U) != 0x80 || sys_read32(RCC + 0x18) != 0 || sys_read32(RCC + 0x1c) != 0x80 || sys_read32(RCC + 0x20) != 0x80 || sys_read32(0x4001047cU) != 3 || !(sys_read32(0x40010448U) & 1) || !(sys_read32(RCC + 0x3c) & 4) || !(sys_read32(RCC + 0x64) & 1) || sys_read32(IWDG + 0x04) != 6 || sys_read32(IWDG + 0x08) != 4095 || (sys_read32(IWDG + 0x0c) & 3)) return false;

    if (usb_disable()) return false;
    wb32_usb_disconnect();
    k_msleep(50);
    k_sched_lock();
    /* PRIMASK, not Zephyr's BASEPRI-only irq_lock: no old ISR or thread may
     * execute after the resident starts overwriting the application slot. */
    __disable_irq();
    sys_write32(0, 0xe000e010U);
    sys_write32(0, 0xe000e014U);
    sys_write32(0, 0xe000e018U);
    for (unsigned bank = 0; bank < 2; ++bank) {
        sys_write32(UINT32_MAX, 0xe000e180U + bank * 4);
        sys_write32(UINT32_MAX, 0xe000e280U + bank * 4);
    }
    sys_write32((1U << 25) | (1U << 27), 0xe000ed04U);
    sys_write32(0, 0xe000ed24U);
    sys_write32(UINT32_MAX, 0xe000ed28U);
    sys_write32(UINT32_MAX, 0xe000ed2cU);
    sys_write32(0x1f, 0xe000ed30U);
    /* Stop the UART source; its NVIC line stays masked until the receiver
     * resets/configures the UART. DLAB is zero in the admitted native state. */
    sys_write32(0, 0x40003804U);
    /* Reset the two DMA controllers before replacing their source buffers.
     * No flash/cache, GPIO, analog, PLL or IWDG domain is reset here. */
    /* Both DMA bridges/controllers need clocks for completion readback. */
    sys_write32(sys_read32(RCC + 0x40) | 0xc0, RCC + 0x40);
    sys_write32(sys_read32(RCC + 0x48) | 0x8001, RCC + 0x48);
    sys_write32(sys_read32(RCC + 0x4c) | 0x811, RCC + 0x4c);
    sys_write32(0, 0x4000904cU); /* SPIM2 TX requests off before DMA reset. */
    sys_write32(1, RCC + 0x90);
    sys_write32(0, RCC + 0x90);
    sys_write32(1, RCC + 0x94);
    sys_write32(0, RCC + 0x94);
    __DSB();
    /* Reset must have stopped every channel before C storage is overwritten.
     * A failed readback returns only to the caller's terminal hardware reset;
     * the old scheduler/USB may never resume after partial preparation. */
    if ((sys_read32(DMAC1 + 0x3a0) & 0xff) || (sys_read32(DMAC2 + 0x3a0) & 0xff)) return false;
    __ISB();
    sys_write32(0xaaaa, IWDG);
    __DSB();
    gmk_zmk_resident_branch(GMK_FLASH_BASE);
}

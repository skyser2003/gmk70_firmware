/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <soc.h>
#include <zephyr/sys/util.h>

/* Native memory only. GPIO starts inaccessible, as when APB1PRE.SRCEN is
 * cleared by the trial role probe. Only an APB1 source-enable write makes
 * production soc.c's direct volatile GPIO accesses possible. */
#define MMIO_BASE 0x40010000U
#define MMIO_BYTES 0x6000U
static uint32_t     registers[MMIO_BYTES / sizeof(uint32_t)];
static size_t       gpio_page_size;
static unsigned int reads;

uint32_t sys_read32(uintptr_t address) {
    assert(++reads < 1000);
    assert(address >= MMIO_BASE && address < MMIO_BASE + MMIO_BYTES);
    assert(address % sizeof(uint32_t) == 0);
    if (address == 0x40010430U) {
        return registers[(0x4001042cU - MMIO_BASE) / 4] & 1U;
    }
    if (address == 0x40010458U) {
        return registers[(0x4001044cU - MMIO_BASE) / 4] & 1U;
    }
    if (address == 0x4001047cU) {
        return registers[(0x40010478U - MMIO_BASE) / 4] & 1U ? 3 : 0;
    }
    return registers[(address - MMIO_BASE) / 4];
}

void sys_write32(uint32_t value, uintptr_t address) {
    assert(address >= MMIO_BASE && address < MMIO_BASE + MMIO_BYTES);
    assert(address % sizeof(uint32_t) == 0);
    registers[(address - MMIO_BASE) / 4] = value;
    if (address == WB32_RCC_BASE + 0x1cU) {
        int protection = value & BIT(7) ? PROT_READ | PROT_WRITE : PROT_NONE;
        assert(mprotect((void *)WB32_GPIOA_BASE, gpio_page_size, protection) == 0);
    }
}

void soc_early_init_hook(void);

int main(void) {
    gpio_page_size = (size_t)sysconf(_SC_PAGESIZE);
    assert(gpio_page_size >= 4096);
    void *const gpio = mmap((void *)WB32_GPIOA_BASE, gpio_page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(gpio == (void *)WB32_GPIOA_BASE);
    assert(sys_read32(WB32_RCC_BASE + 0x1c) == 0);
    soc_early_init_hook();
    assert(sys_read32(WB32_RCC_BASE + 0x1c) == BIT(7));
    assert(sys_read32(WB32_RCC_BASE + 0x20) == BIT(7));
    assert(sys_read32(WB32_RCC_BASE + 0x08) == 2);
    assert(sys_read32(WB32_RCC_BASE) == (BIT(5) | 1U));
    assert(sys_read32(0x40010474U) == (2U << 6));
    assert(sys_read32(0x40015400U) == 0x03000013U);
    assert(((struct wb32_gpio *)WB32_GPIOD_BASE)->moder == 0x0fU);
    assert(sys_read32(0x40010028U) == 0);
    assert(sys_read32(0x4001002cU) == 0);

    wb32_usb_clock_enable();
    assert(sys_read32(0x40014c40U) == 2);
    assert(sys_read32(WB32_RCC_BASE + 0x34) == 1);
    assert(sys_read32(WB32_RCC_BASE + 0x14) == (BIT(3) | BIT(0)));
    assert(((struct wb32_gpio *)WB32_GPIOA_BASE)->moder == ((2U << 22) | (2U << 24)));
    wb32_usb_clock_disable();
    assert(sys_read32(WB32_RCC_BASE + 0x6c) == 0);
    assert(sys_read32(WB32_RCC_BASE + 0x7c) == 0);
    assert(((struct wb32_gpio *)WB32_GPIOA_BASE)->bsrr == BIT(28));
    assert(munmap(gpio, gpio_page_size) == 0);
    puts("WB32 SoC clock tests passed (GPIO blocked until APB1 source enabled)");
    return 0;
}

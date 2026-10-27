/*
 * Copyright (c) 2022 Westberry Technology (ChangZhou) Corp., Ltd.
 * Copyright (c) 2026 GMK70 contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Clock and USB electrical sequences adapted from the pinned vendor-authored
 * WB32FQ95xx hal_lld.c; see firmware/zmk/PROVENANCE.md.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <soc.h>

#define RCC(offset) (WB32_RCC_BASE + (offset))
#define ANCTL(offset) (0x40010400U + (offset))
#define PWR(offset) (0x40010000U + (offset))

static void analog_unlock(void) {
    sys_write32(3, PWR(0x28));
    sys_write32(12, PWR(0x2c));
}

static void analog_lock(void) {
    sys_write32(0, PWR(0x28));
    sys_write32(0, PWR(0x2c));
}

void soc_early_init_hook(void) {
    struct wb32_gpio *const portd = (void *)WB32_GPIOD_BASE;

    analog_unlock();
    /* A staging launch inherits a running PLL. Move to MHSI before changing it. */
    sys_write32(1, ANCTL(0x2c));
    while (!(sys_read32(ANCTL(0x30)) & 1U)) {
    }
    sys_write32(0, RCC(0x08));
    sys_write32(1, RCC(0x0c));
    __DSB();
    __ISB();
    sys_write32(0, ANCTL(0x78));
    sys_write32(0x7be, ANCTL(0x94));

    /* APB source gates can be off after the pre-CRT role probe. Establish
     * AHB/APB clocks before enabling or accessing any GPIO peripheral. */
    sys_write32(0, RCC(0x18));
    sys_write32(BIT(7), RCC(0x1c));
    sys_write32(BIT(7), RCC(0x20));

    sys_set_bits(RCC(0x48), BIT(15) | BIT(8));
    portd->cfgmsk = 0xfffcU;
    portd->moder  = 0x0fU;
    sys_write32(2, ANCTL(0x50));
    sys_write32(1, ANCTL(0x4c));
    while (!(sys_read32(ANCTL(0x58)) & 1U)) {
    }
    /* 12 MHz HSE / 2 * 16 = 96 MHz; AHB/APB1/APB2 divide by one. */
    sys_write32(0x03000013, 0x40015400U); /* cache + prefetch + three wait states */
    sys_write32(1, RCC(0x04));
    sys_write32(BIT(5) | 1U, RCC(0x00));
    sys_write32(2U << 6, ANCTL(0x74));
    sys_write32(1, ANCTL(0x78));
    while (sys_read32(ANCTL(0x7c)) != 3U) {
    }
    sys_write32(2, RCC(0x08));
    sys_write32(1, RCC(0x0c));
    __DSB();
    __ISB();
    analog_lock();
    sys_set_bits(RCC(0x48), BIT(15));
    sys_set_bits(RCC(0x4c), BIT(11));
}

void wb32_usb_clock_enable(void) {
    struct wb32_gpio *const porta = (void *)WB32_GPIOA_BASE;

    sys_set_bits(RCC(0x40), BIT(8) | BIT(1));
    sys_set_bits(RCC(0x88), BIT(1));
    sys_clear_bits(RCC(0x88), BIT(1));
    sys_write32(1, RCC(0x34));
    sys_write32(1, RCC(0x7c));
    sys_write32(2, 0x40014c40U); /* SFM USBPCON: enable USB PHY */
    sys_write32(1, RCC(0x6c));
    sys_write32(BIT(3) | BIT(0), RCC(0x14)); /* PLL / 2 = 48 MHz */
    sys_set_bits(RCC(0x48), BIT(15) | BIT(5));
    porta->cfgmsk  = (uint16_t)~(BIT(11) | BIT(12));
    porta->current = (3U << 22) | (3U << 24);
    porta->moder   = (2U << 22) | (2U << 24);
    porta->otyper  = 0;
    porta->ospeedr = 0;
    porta->pupdr   = 0;
    porta->afrh    = (3U << 12) | (3U << 16);
}

void wb32_usb_disconnect(void) {
    struct wb32_gpio *const porta = (void *)WB32_GPIOA_BASE;

    sys_set_bits(RCC(0x48), BIT(15) | BIT(5));
    porta->cfgmsk  = (uint16_t)~(BIT(11) | BIT(12));
    porta->moder   = 0;
    porta->ospeedr = 0;
    porta->pupdr   = 0;
    porta->cfgmsk  = (uint16_t)~BIT(12);
    porta->moder   = 1U << 24;
    porta->otyper  = BIT(12);
    porta->afrh    = 0;
    porta->bsrr    = BIT(12 + 16);
}

void wb32_usb_clock_disable(void) {
    wb32_usb_disconnect();
    sys_write32(0, RCC(0x14));
    sys_write32(0, RCC(0x6c));
    sys_write32(0, RCC(0x7c));
    sys_clear_bits(RCC(0x40), BIT(1));
}

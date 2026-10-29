/*
 * Copyright (c) 2022 Westberry Technology (ChangZhou) Corp., Ltd.
 * Copyright (c) 2026 GMK70 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * WB32 register sequences adapt the Apache-2.0 vendor-authored SPIv1,
 * DMAv1/wb32_dma.h and WB32FQ95xx CMSIS sources in pinned ChibiOS-Contrib
 * 5a9ad82b6ba4f649cdb800e8c2bbc9be2d3ce767. Four-bit WS2812 encoding follows
 * QMK 08c662f286ddfd12a985f57b584b02eca5af0ae6 ws2812_spi.c. Upstream files
 * remain unchanged. See firmware/zmk/PROVENANCE.md.
 */
#include "wb32_pixels.h"
#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/barrier.h>

#define RCC 0x40010c00U
#define GPIOA 0x40000000U
#define GPIOB 0x40000400U
#define SPIM2 0x40009000U
#define DMAC2 0x4000fc00U
#define BIT32(n) (1U << (n))
#define MAX_PIXELS 41U
#define RESET_US 280U
#define TIMEOUT_MS 5U
#define POLL_LIMIT 100000U

/* DMAC2 channel 0 is exclusively owned by this driver; no DMA IRQ is used.
 * One encoded byte represents two LED bits. The whole physical frame fits
 * the vendor helper's stricter 511-transfer block bound. Reset is GPIO-low. */
static uint8_t encoded[MAX_PIXELS * 12U];
static bool    ready;
K_MUTEX_DEFINE(pixels_lock);

static bool expired(uint32_t started) {
    return (uint32_t)(k_uptime_get_32() - started) >= TIMEOUT_MS;
}

static bool wait_mask(uintptr_t address, uint32_t mask, uint32_t value, uint32_t started) {
    /* A finite iteration limit also bounds a stopped/misconfigured timer. */
    for (unsigned remaining = POLL_LIMIT; remaining; --remaining) {
        if ((sys_read32(address) & mask) == value) return true;
        if (expired(started)) break;
    }
    return false;
}

static void pulse_reset(uint32_t mask) {
    unsigned key = irq_lock();
    sys_set_bits(RCC + 0x94, mask);
    sys_clear_bits(RCC + 0x94, mask);
    (void)sys_read32(RCC + 0x94);
    irq_unlock(key);
}

static void data_pin(bool alternate) {
    unsigned key = irq_lock();
    sys_write32(BIT32(31), GPIOB + 0x18); /* Output latch low before mux changes. */
    sys_write32((uint16_t)~BIT32(15), GPIOB + 0x30);
    sys_write32(0, GPIOB + 0x04); /* Push-pull, no pull, stock high slew/current0. */
    sys_write32(0, GPIOB + 0x08);
    sys_write32(0, GPIOB + 0x0c);
    sys_write32(0, GPIOB + 0x2c);
    sys_write32(5U << 28, GPIOB + 0x24);
    sys_write32((alternate ? 2U : 1U) << 30, GPIOB);
    irq_unlock(key);
}

static void clear_dma(void) {
    for (unsigned offset = 0x338; offset <= 0x358; offset += 8)
        sys_write32(1, DMAC2 + offset);
}

static bool stop_hardware(void) {
    sys_write32(0, SPIM2 + 0x4c);
    sys_write32(0x100, DMAC2 + 0x3a0);
    bool stopped = wait_mask(DMAC2 + 0x3a0, 1, 0, k_uptime_get_32());
    if (!stopped) {
        /* Exclusive ownership permits aborting a failed DMA channel by reset.
         * Buffer reuse is forbidden if even reset cannot prove it stopped. */
        pulse_reset(1);
        stopped = !(sys_read32(DMAC2 + 0x3a0) & 1);
    }
    sys_write32(0, SPIM2 + 0x08);
    sys_write32(0, SPIM2 + 0x10);
    data_pin(false);
    /* Finish all DMA/MMIO observations before the buffer may be rewritten. */
    barrier_dmem_fence_full();
    if (!stopped) {
        sys_write32(0, DMAC2 + 0x398);
        ready = false;
    }
    return stopped;
}

int gmk_pixels_init(void) {
    if (k_is_in_isr()) return -EWOULDBLOCK;
    k_mutex_lock(&pixels_lock, K_FOREVER);
    ready = false;
    k_sched_lock();
    unsigned key = irq_lock();
    sys_set_bits(RCC + 0x40, BIT32(7)); /* DMAC2 AHB bridge. */
    sys_set_bits(RCC + 0x48, BIT32(15) | BIT32(5) | BIT32(6));
    sys_set_bits(RCC + 0x4c, BIT32(0) | BIT32(4));
    (void)sys_read32(RCC + 0x4c);
    irq_unlock(key);
    pulse_reset(BIT32(0) | BIT32(4));
    sys_write32(0, SPIM2 + 0x08);
    sys_write32(0, SPIM2 + 0x4c);
    sys_write32(0, SPIM2 + 0x2c);
    sys_write32(0x100, DMAC2 + 0x3a0);
    clear_dma();
    if (!stop_hardware()) {
        k_sched_unlock();
        k_mutex_unlock(&pixels_lock);
        return -EIO;
    }
    key = irq_lock();
    sys_write32((BIT32(5) | BIT32(8)) << 16, GPIOA + 0x18);
    sys_write32((uint16_t)~(BIT32(5) | BIT32(8)), GPIOA + 0x30);
    sys_write32(0, GPIOA + 0x04);
    sys_write32(0, GPIOA + 0x08);
    sys_write32(0, GPIOA + 0x0c);
    sys_write32(0, GPIOA + 0x2c);
    sys_write32(BIT32(10) | BIT32(16), GPIOA);
    sys_write32(BIT32(5) | BIT32(8), GPIOA + 0x18); /* Both LED rails active high. */
    irq_unlock(key);
    data_pin(true);
    ready = true;
    k_sched_unlock();
    k_mutex_unlock(&pixels_lock);
    return 0;
}

static int wait_dma(uint32_t started) {
    for (unsigned remaining = POLL_LIMIT; remaining; --remaining) {
        if (sys_read32(DMAC2 + 0x2e0) & 1) return -EIO;
        if (sys_read32(DMAC2 + 0x2c0) & 1) {
            /* A simultaneous DMA error must not be hidden by completion. */
            return sys_read32(DMAC2 + 0x2e0) & 1 ? -EIO : 0;
        }
        if (expired(started)) break;
    }
    return -ETIMEDOUT;
}

int gmk_pixels_write(const uint8_t *rgb, size_t count) {
    if (count > MAX_PIXELS || (!rgb && count)) return -EINVAL;
    if (!count) return 0;
    if (k_is_in_isr()) return -EWOULDBLOCK;
    k_mutex_lock(&pixels_lock, K_FOREVER);
    if (!ready) {
        k_mutex_unlock(&pixels_lock);
        return -ENODEV;
    }
    for (size_t byte = 0; byte < count * 3U; ++byte) {
        for (unsigned pair = 0; pair < 4; ++pair) {
            uint8_t source            = rgb[byte] >> (6U - pair * 2U);
            encoded[byte * 4U + pair] = (source & 2 ? 0xe0 : 0x80) | (source & 1 ? 0x0e : 0x08);
        }
    }
    /* Low time is uncritical to scheduling and may safely grow while another
     * worker runs. All subsequent transfer work keeps IRQ service available. */
    data_pin(false);
    k_busy_wait(RESET_US);
    k_sched_lock();
    int result = 0;
    if (!stop_hardware()) {
        result = -EIO;
        goto out;
    }
    data_pin(true);
    sys_write32(0x107, SPIM2); /* 8-bit, mode0, TX-only: do not accumulate RX. */
    sys_write32(0, SPIM2 + 0x04);
    sys_write32(32, SPIM2 + 0x14); /* 96 MHz / 32 = 3 MHz. */
    sys_write32(0, SPIM2 + 0x18);
    sys_write32(0, SPIM2 + 0x1c);
    sys_write32(0, SPIM2 + 0x2c);
    (void)sys_read32(SPIM2 + 0x48);
    sys_write32(2, SPIM2 + 0x50);
    clear_dma();
    for (unsigned offset = 0x310; offset <= 0x330; offset += 8)
        sys_write32(0x100, DMAC2 + offset); /* Mask channel0 interrupts. */
    sys_write32((uint32_t)(uintptr_t)encoded, DMAC2);
    sys_write32(SPIM2 + 0x60, DMAC2 + 0x08);
    sys_write32(0x02100100, DMAC2 + 0x18); /* AHB memory -> fixed APB peripheral. */
    sys_write32((uint32_t)(count * 12U), DMAC2 + 0x1c);
    sys_write32(0, DMAC2 + 0x40);     /* Hardware destination handshake, active high. */
    sys_write32(0x804, DMAC2 + 0x44); /* SPIM2 TX = DMAC2 interface1 (manual p136). */
    sys_write32(0, DMAC2 + 0x48);
    sys_write32(0, DMAC2 + 0x50);
    sys_write32(1, DMAC2 + 0x398);
    sys_write32(1, SPIM2 + 0x08);
    sys_write32(2, SPIM2 + 0x4c);
    uint32_t started = k_uptime_get_32();
    /* Publish the complete SRAM frame before hardware owns the buffer. */
    barrier_dmem_fence_full();
    sys_write32(0x101, DMAC2 + 0x3a0);
    sys_write32(1, SPIM2 + 0x10);
    result = wait_dma(started);
    if (!result && !wait_mask(SPIM2 + 0x28, 5, 4, started)) result = -ETIMEDOUT;
    if (!stop_hardware()) result = -EIO;
out:
    k_sched_unlock();
    k_mutex_unlock(&pixels_lock);
    return result;
}

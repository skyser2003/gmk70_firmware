/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>

static unsigned sched_depth, irq_depth, sched_locks, writes, reads;
static unsigned reset_waits;
static uint32_t now_ms;
static bool     clock_stopped;
int64_t         fake_ms;
void            k_sched_lock(void) {
    assert(!irq_depth);
    ++sched_depth;
    ++sched_locks;
}
void k_sched_unlock(void) {
    assert(sched_depth == 1 && !irq_depth);
    --sched_depth;
}
unsigned test_irq_lock(void) {
    assert(!irq_depth);
    return irq_depth++;
}
void test_irq_unlock(unsigned key) {
    assert(key == 0 && irq_depth == 1);
    --irq_depth;
}
uint32_t test_uptime(void) {
    return now_ms;
}
void test_busy_wait(unsigned us) {
    assert(us >= 280 && !irq_depth);
    ++reset_waits;
}
#define irq_lock test_irq_lock
#define irq_unlock test_irq_unlock
#define k_uptime_get_32 test_uptime
#define k_busy_wait test_busy_wait
#include "../../firmware/zmk/lighting/wb32_pixels.c"

#define HW_SPI 0x40009000U
#define HW_DMA 0x4000fc00U
#define HW_RCC 0x40010c00U
#define HW_A 0x40000000U
#define HW_B 0x40000400U
static uint32_t spi[0x100 / 4], dma[0x400 / 4], rcc[0x100 / 4];
static uint32_t gpio[2][0x34 / 4];
enum fault { NO_FAULT, DMA_ERROR, DMA_TIMEOUT, SPI_BUSY_TIMEOUT, DMA_STOP_TIMEOUT };
static enum fault fault;
static bool       active, stick_on_abort;
static bool       publish_ready, needs_acquire;
static unsigned   barriers;
static unsigned   dma_resets;
static unsigned   progress_reads, busy_reads;
static size_t     captured_size;
static struct {
    uint8_t before[32], bytes[492], after[32];
} capture;

void barrier_dmem_fence_full(void) {
    assert(sched_depth == 1 && !irq_depth);
    ++barriers;
    publish_ready = true;
    if (!active && spi[0x08 / 4] == 0) needs_acquire = false;
}
static uint32_t *reg_at(uintptr_t address) {
    if (address >= HW_SPI && address < HW_SPI + sizeof(spi)) return &spi[(address - HW_SPI) / 4];
    if (address >= HW_DMA && address < HW_DMA + sizeof(dma)) return &dma[(address - HW_DMA) / 4];
    if (address >= HW_RCC && address < HW_RCC + sizeof(rcc)) return &rcc[(address - HW_RCC) / 4];
    if (address >= HW_A && address < HW_A + sizeof(gpio[0])) return &gpio[0][(address - HW_A) / 4];
    if (address >= HW_B && address < HW_B + sizeof(gpio[1])) return &gpio[1][(address - HW_B) / 4];
    assert(!"unexpected MMIO address");
    return NULL;
}
uint32_t sys_read32(uintptr_t address) {
    assert(!irq_depth || (address >= HW_RCC && address < HW_RCC + sizeof(rcc)));
    ++reads;
    if (!clock_stopped && reads % 100 == 0) ++now_ms;
    if (active && address == HW_DMA + 0x2c0 && ++progress_reads == 3) {
        if (fault == DMA_ERROR) {
            dma[0x2e0 / 4] = 1;
            dma[0x3a0 / 4] = 0;
        } else if (fault != DMA_TIMEOUT) {
            assert(sched_depth == 1 && !irq_depth);
            assert(spi[0] == 0x107 && spi[0x14 / 4] == 32);
            assert(spi[0x2c / 4] == 0 && spi[0x4c / 4] == 2);
            assert(dma[0x08 / 4] == HW_SPI + 0x60);
            assert(dma[0x18 / 4] == 0x02100100);
            assert((dma[0x40 / 4] & ~0x60U) == 0 && dma[0x44 / 4] == 0x804);
            assert(dma[0x398 / 4] == 1);
            captured_size = dma[0x1c / 4];
            assert(captured_size && captured_size <= sizeof(capture.bytes));
            memcpy(capture.bytes, (const void *)(uintptr_t)dma[0], captured_size);
            dma[0x2c0 / 4] = 1;
            dma[0x3a0 / 4] = 0;
            active         = false;
        }
    }
    if (address == HW_SPI + 0x28 && !active) {
        ++busy_reads;
        return fault == SPI_BUSY_TIMEOUT || busy_reads < 3 ? 5 : 4;
    }
    if (address == HW_DMA + 0x3a0 && fault == DMA_STOP_TIMEOUT) return 1;
    return *reg_at(address);
}
void sys_write32(uint32_t value, uintptr_t address) {
    ++writes;
    if ((address >= HW_A && address < HW_A + sizeof(gpio[0])) || (address >= HW_B && address < HW_B + sizeof(gpio[1]))) {
        unsigned p      = address >= HW_B;
        unsigned offset = (unsigned)(address - (p ? HW_B : HW_A));
        if (offset == 0x18) {
            gpio[p][0x14 / 4] = (gpio[p][0x14 / 4] | (value & 0xffff)) & ~(value >> 16);
        } else if (offset == 0x30) {
            assert(irq_depth == 1);
            gpio[p][offset / 4] = value;
        } else {
            assert(irq_depth == 1);
            uint32_t result   = gpio[p][offset / 4];
            uint32_t unmasked = ~gpio[p][0x30 / 4] & 0xffff;
            for (unsigned pin = 0; pin < 16; ++pin) {
                unsigned width = offset == 0x04 ? 1 : offset == 0x20 || offset == 0x24 ? 4 : 2;
                if (!(unmasked & (1U << pin)) || (offset == 0x20 && pin >= 8) || (offset == 0x24 && pin < 8)) continue;
                unsigned shift = width * (width == 4 ? pin % 8 : pin);
                uint32_t mask  = ((1U << width) - 1) << shift;
                result         = (result & ~mask) | (value & mask);
            }
            gpio[p][offset / 4] = result;
        }
        return;
    }
    if (address == HW_RCC + 0x94 && (value & 1) && !(*reg_at(address) & 1)) {
        memset(dma, 0, sizeof(dma));
        active = stick_on_abort = false;
        ++dma_resets;
    }
    if (address >= HW_DMA + 0x338 && address <= HW_DMA + 0x358 && (address - HW_DMA - 0x338) % 8 == 0) {
        dma[(0x2c0 + address - HW_DMA - 0x338) / 4] &= ~value;
        return;
    }
    if (address == HW_DMA + 0x3a0) {
        assert(sched_depth == 1);
        if (value & 1) {
            assert(publish_ready);
            needs_acquire  = true;
            dma[0x3a0 / 4] = value & 1;
            active         = true;
            progress_reads = 0;
        } else if (!(stick_on_abort && active) && fault != DMA_STOP_TIMEOUT) {
            dma[0x3a0 / 4] = 0;
            active         = false;
        }
        return;
    }
    if (address == HW_SPI + 0x10 && value == 1) assert(sched_depth == 1);
    if (address == HW_DMA) publish_ready = false;
    *reg_at(address) = value;
}
static void clear_model(void) {
    memset(spi, 0, sizeof(spi));
    memset(dma, 0, sizeof(dma));
    memset(rcc, 0, sizeof(rcc));
    memset(gpio, 0xa5, sizeof(gpio));
    memset(&capture, 0xc3, sizeof(capture));
    rcc[0x40 / 4] = 0x10000000;
    rcc[0x48 / 4] = 0x10000000;
    rcc[0x4c / 4] = 0x10000000;
    captured_size = writes = reads = sched_depth = irq_depth = sched_locks = now_ms = reset_waits = 0;
    active = clock_stopped = stick_on_abort = false;
    dma_resets = barriers = 0;
    publish_ready = needs_acquire = false;
    progress_reads = busy_reads = 0;
    fault                       = NO_FAULT;
}
static void assert_clean(void) {
    assert(spi[0x08 / 4] == 0 && spi[0x4c / 4] == 0);
    assert((dma[0x3a0 / 4] & 1) == 0);
    assert((gpio[1][0] >> 30) == 1 && !(gpio[1][0x14 / 4] & (1U << 15)));
    assert(!sched_depth && !irq_depth && !needs_acquire);
    assert(barriers >= 2);
    for (unsigned i = 0; i < 32; ++i)
        assert(capture.before[i] == 0xc3 && capture.after[i] == 0xc3);
}
static void test_init_clocks_and_pins(void) {
    clear_model();
    assert(gmk_pixels_init() == 0);
    assert((rcc[0x40 / 4] & 0x10000080) == 0x10000080);
    assert((rcc[0x48 / 4] & 0x10008060) == 0x10008060);
    assert((rcc[0x4c / 4] & 0x10000011) == 0x10000011);
    assert(!(rcc[0x94 / 4] & 0x11));
    assert(((gpio[0][0] >> 10) & 3) == 1 && ((gpio[0][0] >> 16) & 3) == 1);
    assert((gpio[0][0x14 / 4] & 0x120) == 0x120);
    assert((gpio[1][0] >> 30) == 2 && (gpio[1][0x24 / 4] >> 28) == 5);
    assert(!(gpio[1][0x04 / 4] & (1U << 15)) && !(gpio[1][0x0c / 4] >> 30));
    /* A maintained matrix pin on the same port remains untouched. */
    assert(((gpio[1][0] >> 24) & 3) == ((0xa5a5a5a5U >> 24) & 3));
    assert(!sched_depth && !irq_depth);
}
static void test_rgb_encoding_and_guards(void) {
    uint8_t              values[12] = {0xff, 0, 0, 0, 0xff, 0, 0, 0, 0xff, 0x80, 0x01, 0x55};
    static const uint8_t expected[] = {0xee, 0xee, 0xee, 0xee, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0xee, 0xee, 0xee, 0xee, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0xee, 0xee, 0xee, 0xee, 0xe8, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x8e, 0x8e, 0x8e, 0x8e, 0x8e};
    clear_model();
    assert(gmk_pixels_init() == 0);
    dma[0x2c0 / 4] = 1; /* Prior completion must be cleared before enabling. */
    assert(gmk_pixels_write(values, 4) == 0);
    assert(captured_size == sizeof(expected) && memcmp(capture.bytes, expected, sizeof(expected)) == 0);
    assert(reset_waits && busy_reads >= 3);
    assert_clean();
    struct {
        uint8_t before[32], rgb[123], after[32];
    } input;
    memset(&input, 0x5a, sizeof(input));
    memset(input.rgb, 0xff, sizeof(input.rgb));
    assert(gmk_pixels_write(input.rgb, 41) == 0 && captured_size == 492);
    for (unsigned i = 0; i < captured_size; ++i)
        assert(capture.bytes[i] == 0xee);
    for (unsigned i = 0; i < 32; ++i)
        assert(input.before[i] == 0x5a && input.after[i] == 0x5a);
    assert_clean();
}
static void test_stuck_abort_resets_dma_before_buffer_reuse(void) {
    uint8_t rgb[3] = {0xff, 0, 0};
    clear_model();
    assert(gmk_pixels_init() == 0);
    unsigned resets_before = dma_resets;
    fault                  = DMA_TIMEOUT;
    stick_on_abort         = true;
    assert(gmk_pixels_write(rgb, 1) == -ETIMEDOUT);
    assert(dma_resets == resets_before + 1);
    assert_clean();
    fault = NO_FAULT;
    assert(gmk_pixels_write(rgb, 1) == 0 && captured_size == 12);
    assert_clean();
}
static void test_uptime_wrap_is_bounded(void) {
    uint8_t rgb[3] = {0, 0xff, 0};
    clear_model();
    assert(gmk_pixels_init() == 0);
    now_ms = UINT32_MAX - 2;
    fault  = DMA_TIMEOUT;
    assert(gmk_pixels_write(rgb, 1) == -ETIMEDOUT);
    assert(now_ms < 10 && reads < 1000);
    assert_clean();
}
static void test_initial_dma_failure_is_closed(void) {
    clear_model();
    fault = DMA_STOP_TIMEOUT;
    assert(gmk_pixels_init() == -EIO);
    assert(!sched_depth && !irq_depth);
    assert(spi[0x08 / 4] == 0 && spi[0x4c / 4] == 0 && dma[0x398 / 4] == 0);
    assert((gpio[1][0] >> 30) == 1 && !(gpio[1][0x14 / 4] & (1U << 15)));
    uint8_t  rgb[3] = {1, 2, 3};
    unsigned before = writes;
    assert(gmk_pixels_write(rgb, 1) == -ENODEV && writes == before);
}
static void test_invalid_requests_are_quiet(void) {
    uint8_t  rgb[126] = {0};
    unsigned before   = writes;
    assert(gmk_pixels_write(rgb, 42) == -EINVAL);
    assert(gmk_pixels_write(NULL, 1) == -EINVAL);
    assert(gmk_pixels_write(rgb, SIZE_MAX) == -EINVAL);
    assert(gmk_pixels_write(NULL, 0) == 0);
    assert(writes == before);
}
static void test_failure_cleanup(enum fault failure) {
    uint8_t rgb[3] = {1, 2, 3};
    clear_model();
    assert(gmk_pixels_init() == 0);
    fault = failure;
    assert(gmk_pixels_write(rgb, 1) == (failure == DMA_ERROR ? -EIO : -ETIMEDOUT));
    assert(reads < 400000 && sched_locks >= 1);
    assert_clean();
    fault      = NO_FAULT;
    busy_reads = 0;
    assert(gmk_pixels_write(rgb, 1) == 0);
    assert(captured_size == 12);
    assert_clean();
}
int main(void) {
    uint8_t initial[3] = {1, 2, 3};
    assert(gmk_pixels_write(initial, 1) == -ENODEV && writes == 0);
    test_init_clocks_and_pins();
    test_rgb_encoding_and_guards();
    test_invalid_requests_are_quiet();
    test_failure_cleanup(DMA_ERROR);
    test_failure_cleanup(DMA_TIMEOUT);
    test_failure_cleanup(SPI_BUSY_TIMEOUT);
    test_initial_dma_failure_is_closed();
    test_stuck_abort_resets_dma_before_buffer_reuse();
    test_uptime_wrap_is_bounded();
    clear_model();
    assert(gmk_pixels_init() == 0);
    clock_stopped  = true;
    fault          = DMA_TIMEOUT;
    uint8_t rgb[3] = {1, 2, 3};
    assert(gmk_pixels_write(rgb, 1) == -ETIMEDOUT);
    assert(reads < 400000);
    assert_clean();
    puts("WB32 pixels: encoding, bounds, GPIO, DMA and finite cleanup passed");
}

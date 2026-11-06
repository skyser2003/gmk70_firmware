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
static uint64_t now_us;
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
    now_us += us;
    ++reset_waits;
}
#define irq_lock test_irq_lock
#define irq_unlock test_irq_unlock
#define k_uptime_get_32 test_uptime
#define k_busy_wait test_busy_wait
#include "../../firmware/zmk/lighting/wb32_pixels.c"
#ifdef TEST_PIXELS_LIGHTING
#    include "../../firmware/zmk/lighting/lighting.c"
#endif

#define HW_SPI 0x40009000U
#define HW_DMA 0x4000fc00U
#define HW_RCC 0x40010c00U
#define HW_A 0x40000000U
#define HW_B 0x40000400U
static uint32_t spi[0x100 / 4], dma[0x400 / 4], rcc[0x100 / 4];
static uint32_t gpio[2][0x34 / 4];
enum fault { NO_FAULT, DMA_ERROR, DMA_TIMEOUT, SPI_BUSY_TIMEOUT, DMA_STOP_TIMEOUT };
static enum fault fault;
static bool       active, stick_on_abort, reset_cannot_stop;
static bool       publish_ready, needs_acquire;
static unsigned   barriers;
static unsigned   dma_resets;
static unsigned   progress_reads, busy_reads;
static uint64_t   data_low_since, powered_since;
static unsigned   transfers, rail_disables;
static bool       observe_rails;
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
            uint32_t previous = gpio[p][0x14 / 4];
            gpio[p][0x14 / 4] = (gpio[p][0x14 / 4] | (value & 0xffff)) & ~(value >> 16);
            if (observe_rails && p == 0 && ((previous ^ gpio[p][0x14 / 4]) & 0x120)) {
                assert(pixels_lock.locked == 1);
                assert((gpio[1][0] >> 30) == 1 && !(gpio[1][0x14 / 4] & (1U << 15)));
                assert(!active && spi[0x08 / 4] == 0 && spi[0x4c / 4] == 0);
                if (gpio[p][0x14 / 4] & 0x120) {
                    powered_since = now_us;
                } else {
                    assert(now_us - data_low_since >= 280);
                    ++rail_disables;
                }
            }
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
            if (p == 1 && offset == 0 && (result >> 30) == 1 && (gpio[p][offset / 4] >> 30) != 1) data_low_since = now_us;
            gpio[p][offset / 4] = result;
        }
        return;
    }
    if (address == HW_RCC + 0x94 && (value & 1) && !(*reg_at(address) & 1)) {
        if (!reset_cannot_stop) {
            memset(dma, 0, sizeof(dma));
            active = stick_on_abort = false;
        }
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
            if (observe_rails) {
                assert(pixels_lock.locked == 1);
                assert((gpio[0][0x14 / 4] & 0x120) == 0x120);
                /* A data reset does not cover LED supply startup. A single
                 * indicator frame must wait for the restored rail, rather
                 * than relying on a later animation/peer replay to be seen. */
                assert(now_us - powered_since >= 1000 && now_us - data_low_since >= 280);
            }
            ++transfers;
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
    active = clock_stopped = stick_on_abort = reset_cannot_stop = false;
    dma_resets = barriers = 0;
    publish_ready = needs_acquire = false;
    progress_reads = busy_reads = 0;
    now_us = data_low_since = powered_since = 0;
    transfers = rail_disables = 0;
    observe_rails             = false;
    fault                     = NO_FAULT;
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
    assert((gpio[0][0x14 / 4] & 0x120) == 0);
    assert((gpio[1][0] >> 30) == 1 && (gpio[1][0x24 / 4] >> 28) == 5);
    assert(!(gpio[1][0x04 / 4] & (1U << 15)) && !(gpio[1][0x0c / 4] >> 30));
    /* A maintained matrix pin on the same port remains untouched. */
    assert(((gpio[1][0] >> 24) & 3) == ((0xa5a5a5a5U >> 24) & 3));
    assert(!sched_depth && !irq_depth);
}
static void test_black_frame_cuts_physical_power_after_latch(void) {
    uint8_t lit[123]   = {0};
    uint8_t black[123] = {0};
    for (size_t count = 36; count <= 41; count += 5) {
        clear_model();
        assert(gmk_pixels_init() == 0);
        memset(lit, 0, sizeof(lit));
        lit[count * 3 - 1] = 1; /* A single blue bit in the final local LED needs power. */
        observe_rails      = true;
        assert(gmk_pixels_write(lit, count) == 0);
        assert((gpio[0][0x14 / 4] & 0x120) == 0x120);
        unsigned before = transfers;
        assert(gmk_pixels_write(black, count) == 0);
        assert(transfers == before + 1 && rail_disables == 1);
        assert((gpio[0][0x14 / 4] & 0x120) == 0);
        assert_clean();
        assert(gmk_pixels_write(black, count) == 0);
        assert((gpio[0][0x14 / 4] & 0x120) == 0);
        assert(gmk_pixels_write(lit, count) == 0);
        assert((gpio[0][0x14 / 4] & 0x120) == 0x120);
        assert_clean();
    }
}
static void test_led_power_preserves_encoder_pin_configuration(void) {
    clear_model();
    uint32_t before[sizeof(gpio[1]) / sizeof(*gpio[1])];
    memcpy(before, gpio[1], sizeof(before));
    assert(gmk_pixels_init() == 0);
    uint8_t lit[3] = {1, 2, 3}, black[3] = {0};
    assert(gmk_pixels_write(lit, 1) == 0);
    assert(gmk_pixels_write(black, 1) == 0);
    assert(gmk_pixels_write(lit, 1) == 0);
    // B6/B7 retain their GPIO modes, pulls, output type, speed and alternate
    // configuration while the pixel driver changes B15 and the LED supplies.
    for (unsigned pin = 6; pin <= 7; ++pin) {
        const unsigned offsets[] = {0, 0x04, 0x08, 0x0c, 0x20, 0x2c};
        for (unsigned i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i) {
            unsigned offset = offsets[i];
            unsigned width  = offset == 0x04 ? 1 : offset == 0x20 ? 4 : 2;
            uint32_t mask   = ((1U << width) - 1) << (width * pin);
            assert((gpio[1][offset / 4] & mask) == (before[offset / 4] & mask));
        }
    }
    assert_clean();
}
static void test_failed_black_frame_retries_before_power_off(enum fault failure) {
    uint8_t lit[3]   = {0, 0, 1};
    uint8_t black[3] = {0};
    clear_model();
    assert(gmk_pixels_init() == 0);
    observe_rails = true;
    assert(gmk_pixels_write(lit, 1) == 0);
    fault = failure;
    assert(gmk_pixels_write(black, 1) == (failure == DMA_ERROR ? -EIO : -ETIMEDOUT));
    assert((gpio[0][0x14 / 4] & 0x120) == 0x120 && rail_disables == 0);
    assert_clean();
    fault      = NO_FAULT;
    busy_reads = 0;
    assert(gmk_pixels_write(black, 1) == 0);
    assert((gpio[0][0x14 / 4] & 0x120) == 0 && rail_disables == 1);
    assert_clean();
    fault = DMA_ERROR;
    assert(gmk_pixels_write(lit, 1) == -EIO);
    assert((gpio[0][0x14 / 4] & 0x120) == 0x120);
    fault = NO_FAULT;
    assert(gmk_pixels_write(lit, 1) == 0);
    assert_clean();
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

/* Moving encoded[] to a returning stack frame would leave this DMA pointer
 * dangling after an unprovable abort. Preserve the buffer and close reentry. */
static void test_unrecoverable_active_dma_keeps_its_buffer_alive(void) {
    clear_model();
    assert(gmk_pixels_init() == 0);
    fault          = DMA_TIMEOUT;
    stick_on_abort = reset_cannot_stop = true;
    const uint8_t rgb[3]               = {0xff, 0, 0};
    assert(gmk_pixels_write(rgb, 1) == -EIO);
    assert(active && !ready && dma[0x398 / 4] == 0);
    const uint8_t *borrowed     = (const uint8_t *)(uintptr_t)dma[0];
    const uint8_t  expected[12] = {0xee, 0xee, 0xee, 0xee, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88};
    assert(memcmp(borrowed, expected, sizeof(expected)) == 0);
    const uint8_t replacement[3] = {0, 0xff, 0xff};
    assert(gmk_pixels_write(replacement, 1) == -ENODEV);
    assert(memcmp(borrowed, expected, sizeof(expected)) == 0);
    assert(!sched_depth && !irq_depth);
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
    assert((gpio[0][0x14 / 4] & 0x120) == 0);
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
#ifdef TEST_PIXELS_LIGHTING
static bool     left_role = true;
static uint8_t  peer_packets[GMK_LIGHT_CHUNKS][16];
static unsigned peer_packets_count;
bool            gmk_wired_is_left(void) {
    return left_role;
}
bool gmk_split_available(void) {
    return true;
}
void gmk_battery_get(struct gmk_battery_status *status) {
    *status = (struct gmk_battery_status){.valid = true, .percentage = 100, .cable = true};
}
void gmk_battery_request(void) {}
bool __wrap_gmk_protocol_lighting(const struct gmk_wire_io *io, const uint8_t packet[16]) {
    (void)io;
    assert(peer_packets_count < GMK_LIGHT_CHUNKS);
    memcpy(peer_packets[peer_packets_count++], packet, 16);
    return true;
}
static void collect_peer_frame(uint8_t packets[GMK_LIGHT_CHUNKS][16]) {
    peer_packets_count = 0;
    for (unsigned i = 0; i < GMK_LIGHT_CHUNKS; ++i)
        gmk_lighting_wire_poll(NULL);
    assert(peer_packets_count == GMK_LIGHT_CHUNKS);
    memcpy(packets, peer_packets, sizeof(peer_packets));
}
static void test_battery_overlay_releases_physical_power_on_both_roles(uint32_t position, enum gmk_battery_indicator kind) {
    uint8_t        overlay[GMK_LIGHT_CHUNKS][16], released[GMK_LIGHT_CHUNKS][16];
    struct led_rgb colors[GMK_LIGHT_PIXELS] = {0};
    clear_model();
    assert(init(NULL) == 0);
    observe_rails = true;
    assert(update_rgb(NULL, colors, GMK_LIGHT_PIXELS) == 0);
    assert((gpio[0][0x14 / 4] & 0x120) == 0);
    assert(gmk_lighting_battery_set(kind, position, true) == 0);
    gmk_lighting_poll();
    assert((gpio[0][0x14 / 4] & 0x120) == 0x120);
    /* The first waveform after supply-off can be lost even though DMA succeeds.
     * An unchanged held indicator must receive another local frame, without
     * needing a second battery key or a normal upstream animation tick. */
    unsigned first_frame_transfers = transfers;
    now_ms += 50;
    gmk_lighting_poll();
    assert(transfers == first_frame_transfers + 1);
    collect_peer_frame(overlay);
    assert(gmk_lighting_battery_release(position) == 0);
    gmk_lighting_poll();
    assert((gpio[0][0x14 / 4] & 0x120) == 0);
    collect_peer_frame(released);
    unsigned released_transfers = transfers;
    now_ms += 250;
    gmk_lighting_poll();
    assert(transfers == released_transfers && (gpio[0][0x14 / 4] & 0x120) == 0);
    for (unsigned i = 0; i < sizeof(normal_pixels); ++i)
        assert(normal_pixels[i] == 0);
    left_role = false;
    memset(&frame, 0, sizeof(frame));
    for (unsigned i = 0; i < GMK_LIGHT_CHUNKS; ++i)
        gmk_lighting_receive(overlay[i]);
    gmk_lighting_poll();
    if (kind == GMK_BATTERY_LEVEL) assert(captured_size == 492);
    assert((gpio[0][0x14 / 4] & 0x120) == (kind == GMK_BATTERY_LEVEL ? 0x120U : 0U));
    for (unsigned i = 0; i < GMK_LIGHT_CHUNKS; ++i)
        gmk_lighting_receive(released[i]);
    gmk_lighting_poll();
    assert((gpio[0][0x14 / 4] & 0x120) == 0);
    assert_clean();
    left_role = true;
}
#endif
int main(void) {
    uint8_t initial[3] = {1, 2, 3};
    assert(gmk_pixels_write(initial, 1) == -ENODEV && writes == 0);
#ifdef TEST_PIXELS_LIGHTING
    const uint32_t positions[] = {55, GMK_PHYSICAL_KEYS, GMK_KEY_POSITIONS - 1};
    for (unsigned i = 0; i < sizeof(positions) / sizeof(*positions); ++i) {
        test_battery_overlay_releases_physical_power_on_both_roles(positions[i], GMK_BATTERY_LEVEL);
        test_battery_overlay_releases_physical_power_on_both_roles(positions[i], GMK_BATTERY_CHARGING);
    }
#endif
    test_black_frame_cuts_physical_power_after_latch();
    test_led_power_preserves_encoder_pin_configuration();
    test_init_clocks_and_pins();
    test_failed_black_frame_retries_before_power_off(DMA_ERROR);
    test_failed_black_frame_retries_before_power_off(DMA_TIMEOUT);
    test_failed_black_frame_retries_before_power_off(SPI_BUSY_TIMEOUT);
    test_rgb_encoding_and_guards();
    test_invalid_requests_are_quiet();
    test_failure_cleanup(DMA_ERROR);
    test_failure_cleanup(DMA_TIMEOUT);
    test_failure_cleanup(SPI_BUSY_TIMEOUT);
    test_initial_dma_failure_is_closed();
    test_stuck_abort_resets_dma_before_buffer_reuse();
    test_unrecoverable_active_dma_keeps_its_buffer_alive();
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

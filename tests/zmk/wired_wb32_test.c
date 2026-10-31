/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <zephyr/kernel.h>
#include "../../firmware/zmk/drivers/gpio/gpio_wb32.c"
static struct wb32_gpio_data         gpio_data[3];
static const struct wb32_gpio_config gpio_config[3]     = {{{0xffff}, (void *)WB32_GPIOA_BASE, 5}, {{0xffff}, (void *)WB32_GPIOB_BASE, 6}, {{0xffff}, (void *)WB32_GPIOC_BASE, 7}};
const struct device                  fake_gpio_ports[3] = {{&gpio_data[0], &wb32_gpio_api, &gpio_config[0]}, {&gpio_data[1], &wb32_gpio_api, &gpio_config[1]}, {&gpio_data[2], &wb32_gpio_api, &gpio_config[2]}};
#include "../../firmware/zmk/wired/wired_wb32.c"
int64_t fake_ms;
void (*fake_uart_isr)(const void *);
static uint8_t  hw_rx[64];
static size_t   hw_pos, hw_size;
static uint32_t hw_error;
static bool     saw_divider;
static bool     emulate_peer;
static unsigned uart_transmissions;
static bool     arm_race;
int             gpio_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags) {
    int result = wb32_interrupt_configure(dev, pin, flags ? GPIO_INT_MODE_EDGE : GPIO_INT_MODE_DISABLED, GPIO_INT_TRIG_LOW);
    if (flags && arm_race && pin == 13) {
        /* A press arrives while the final row is being armed, before any callback. */
        ((struct wb32_gpio *)WB32_GPIOA_BASE)->idr &= ~BIT(1);
        arm_race = false;
    }
    return result;
}
static const uint8_t peer_matrix[10] = {1, 0, 0, 1, 2, 0, 3, 0, 4, 0};
uint32_t             sys_read32(uintptr_t address) {
    if (address == REG(0x14)) return hw_error | (hw_pos < hw_size ? 1 : 0);
    if (address == REG(0)) {
        assert(hw_pos < hw_size);
        return hw_rx[hw_pos++];
    }
    return *(uint32_t *)address;
}
void sys_write32(uint32_t value, uintptr_t address) {
    if (address == EXTI_PR) {
        *(uint32_t *)address &= ~value;
        return;
    }
    if (address == REG(0) && *(uint32_t *)REG(0xc) != 0x80) ++uart_transmissions;
    if (address == REG(0) && *(uint32_t *)REG(0xc) == 0x80) {
        assert(value == 52);
        saw_divider = true;
    } else if (address == REG(0) && emulate_peer) {
        assert(value == 0 || value == 1);
        uint8_t ack = (uint8_t)(value ^ 19);
        assert(k_msgq_put(&uart_rx, &ack, K_NO_WAIT) == 0);
        if (value == 0) {
            uint8_t checksum = gmk_wire_crc8(peer_matrix, sizeof(peer_matrix));
            assert(k_msgq_put(&uart_rx, &checksum, K_NO_WAIT) == 0);
        } else {
            for (unsigned i = 0; i < sizeof(peer_matrix); ++i)
                assert(k_msgq_put(&uart_rx, &peer_matrix[i], K_NO_WAIT) == 0);
        }
    }
    *(uint32_t *)address = value;
}
int k_msgq_get(struct k_msgq *q, void *out, int timeout) {
    if (!q->count) {
        if (timeout > 0) fake_ms += timeout;
        return -EAGAIN;
    }
    *(uint8_t *)out = q->bytes[q->head++ % 128];
    --q->count;
    return 0;
}
int k_msgq_put(struct k_msgq *q, const void *in, int timeout) {
    (void)timeout;
    if (q->count == 128) return -ENOSPC;
    q->bytes[q->tail++ % 128] = *(const uint8_t *)in;
    ++q->count;
    return 0;
}
void k_msgq_purge(struct k_msgq *q) {
    q->count = q->head = q->tail = 0;
}
static void check_host_modes(struct wb32_gpio *gpio) {
    for (uint8_t physical = 0; physical < 4; ++physical) {
        gpio->idr = (gpio->idr & ~((1U << 14) | (1U << 15))) | ((physical & 2U) ? 1U << 14 : 0) | ((physical & 1U) ? 1U << 15 : 0);
        assert(gmk_host_switch() == physical);
        assert(gmk_wired_mode() == (physical == 3));
#ifdef CONFIG_GMK70_RADIO
        bool available = gmk_wired_is_left() ? physical != 0 : physical == 3;
#else
        bool available = physical == 3;
#endif
        assert(gmk_split_available() == available);
    }
}
static unsigned         gpio_events[3];
static gpio_port_pins_t gpio_seen[3];
static void             gpio_event(const struct device *dev, struct gpio_callback *cb, gpio_port_pins_t pins) {
    (void)cb;
    unsigned port = (unsigned)(dev - fake_gpio_ports);
    assert(port < 3);
    ++gpio_events[port];
    gpio_seen[port] |= pins;
}
static void test_exti(void) {
    struct gpio_callback     callbacks[3];
    const unsigned           pins[3]     = {1, 6, 13};
    const enum gpio_int_trig triggers[3] = {GPIO_INT_TRIG_LOW, GPIO_INT_TRIG_BOTH, GPIO_INT_TRIG_HIGH};
    assert((sys_read32(WB32_RCC_BASE + 0x48) & (BIT(9) | BIT(10))) == (BIT(9) | BIT(10)));
    for (unsigned port = 0; port < 3; ++port) {
        gpio_init_callback(&callbacks[port], gpio_event, BIT(pins[port]));
        assert(gpio_add_callback(&fake_gpio_ports[port], &callbacks[port]) == 0);
        assert(wb32_interrupt_configure(&fake_gpio_ports[port], pins[port], GPIO_INT_MODE_EDGE, triggers[port]) == 0);
        assert(((sys_read32(AFIO_EXTICR(pins[port])) >> (pins[port] % 4 * 4)) & 15) == port);
    }
    assert((sys_read32(EXTI_RTSR) & (BIT(1) | BIT(6) | BIT(13))) == (BIT(6) | BIT(13)));
    assert((sys_read32(EXTI_FTSR) & (BIT(1) | BIT(6) | BIT(13))) == (BIT(1) | BIT(6)));
    assert(wb32_interrupt_configure(&fake_gpio_ports[1], 1, GPIO_INT_MODE_EDGE, GPIO_INT_TRIG_LOW) == -EBUSY);
    assert(wb32_interrupt_configure(&fake_gpio_ports[1], 1, GPIO_INT_MODE_DISABLED, GPIO_INT_TRIG_LOW) == 0);
    assert(exti_owner[1] == &fake_gpio_ports[0]);
    assert(wb32_interrupt_configure(&fake_gpio_ports[0], 16, GPIO_INT_MODE_EDGE, GPIO_INT_TRIG_LOW) == -EINVAL);
    assert(wb32_interrupt_configure(&fake_gpio_ports[0], 0, GPIO_INT_MODE_LEVEL, GPIO_INT_TRIG_LOW) == -ENOTSUP);
    *(uint32_t *)EXTI_PR = BIT(1) | BIT(6) | BIT(13) | BIT(16);
    wb32_exti_isr((void *)BIT(1));
    assert(gpio_events[0] == 1 && gpio_events[1] == 0 && gpio_events[2] == 0);
    assert(sys_read32(EXTI_PR) == (BIT(6) | BIT(13) | BIT(16)));
    wb32_exti_isr((void *)0x3e0);
    wb32_exti_isr((void *)0xfc00);
    assert(sys_read32(EXTI_PR) == BIT(16)); /* Preserve non-GPIO pending sources. */
    for (unsigned port = 0; port < 3; ++port) {
        assert(gpio_events[port] == 1 && gpio_seen[port] == BIT(pins[port]));
        assert(wb32_interrupt_configure(&fake_gpio_ports[port], pins[port], GPIO_INT_MODE_DISABLED, GPIO_INT_TRIG_LOW) == 0);
        assert(wb32_manage_callback(&fake_gpio_ports[port], &callbacks[port], false) == 0);
    }
    assert(wb32_interrupt_configure(&fake_gpio_ports[1], 1, GPIO_INT_MODE_EDGE, GPIO_INT_TRIG_LOW) == 0);
    assert(exti_owner[1] == &fake_gpio_ports[1]);
    assert(wb32_interrupt_configure(&fake_gpio_ports[1], 1, GPIO_INT_MODE_DISABLED, GPIO_INT_TRIG_LOW) == 0);
}
static void wake_pin(unsigned pin) {
    *(uint32_t *)EXTI_PR |= BIT(pin);
    wb32_exti_isr((void *)(uintptr_t)BIT(pin));
}
static void test_idle_matrix(bool central) {
    struct wb32_gpio *a = (void *)WB32_GPIOA_BASE, *b = (void *)WB32_GPIOB_BASE, *c = (void *)WB32_GPIOC_BASE;
    matrix_idle_end();
    memset(&debounce, 0, sizeof(debounce));
    a->idr = c->idr = 0xffff;
    b->idr          = central ? BIT(9) : 0;
    assert(wired_init() == 0);
    uint16_t rows[5];
    gmk_wired_scan(rows);
    assert(scan_idle && scan_irq_ready && !atomic_get(&scan_pending));
    uint32_t armed = BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(13);
    assert((sys_read32(EXTI_IMR) & armed) == armed);
    int64_t quiet_start = fake_ms;
    for (unsigned i = 0; i < 1000; ++i) {
        gmk_wired_scan(rows);
        for (unsigned r = 0; r < 5; ++r)
            assert(rows[r] == 0);
    }
    assert(fake_ms == quiet_start); /* No column-drive/settle sweeps while idle. */
    /* A queued edge remains actionable even if the input is high by dispatch. */
    wake_pin(2);
    assert(atomic_get(&scan_pending));
    gmk_wired_scan(rows);
    assert(fake_ms > quiet_start && scan_idle && !atomic_get(&scan_pending));
    if (central) {
        emulate_peer = true;
        gmk_wired_poll();
        emulate_peer = false;
        assert(gmk_wired_remote_matrix(rows) && rows[0] == 1 && rows[4] == 4);
        assert(scan_idle); /* A remote press does not require a local press. */
    }
    a->idr &= ~BIT(1);
    wake_pin(1);
    assert(atomic_get(&scan_pending));
    gmk_wired_scan(rows);
    assert(!scan_idle && rows[0] == 0);
    gmk_wired_scan(rows);
    uint16_t columns = central ? 0xff : 0x1ff;
    assert(rows[0] == columns);
    for (unsigned i = 0; i < 10; ++i) {
        gmk_wired_scan(rows);
        assert(rows[0] == columns && !scan_idle); /* Held modifiers keep scanning. */
    }
    a->idr |= BIT(1);
    gmk_wired_scan(rows);
    assert(rows[0] == columns && !scan_idle); /* Release must finish debounce. */
    gmk_wired_scan(rows);
    assert(rows[0] == 0 && scan_idle);
    c->idr &= ~BIT(13);
    wake_pin(13);
    gmk_wired_scan(rows);
    gmk_wired_scan(rows);
    assert(rows[4] == columns && !scan_idle);
    c->idr |= BIT(13);
    gmk_wired_scan(rows);
    gmk_wired_scan(rows);
    assert(scan_idle && rows[4] == 0);

    /* Existing-down input during arm must be noticed even without a callback. */
    matrix_idle_end();
    arm_race = true;
    gmk_wired_scan(rows);
    assert(scan_idle && atomic_get(&scan_pending));
    gmk_wired_scan(rows);
    gmk_wired_scan(rows);
    assert(rows[0] == columns && !scan_idle);
    a->idr |= BIT(1);
    gmk_wired_scan(rows);
    gmk_wired_scan(rows);
    assert(scan_idle);

    /* A different port owns EXTI13: preserve it, undo partial row arms and poll. */
    matrix_idle_end();
    assert(wb32_interrupt_configure(&fake_gpio_ports[1], 13, GPIO_INT_MODE_EDGE, GPIO_INT_TRIG_LOW) == 0);
    gmk_wired_scan(rows);
    assert(!scan_idle && !scan_irq_ready && exti_owner[13] == &fake_gpio_ports[1]);
    assert((sys_read32(EXTI_IMR) & armed) == BIT(13));
    assert(wb32_interrupt_configure(&fake_gpio_ports[1], 13, GPIO_INT_MODE_DISABLED, GPIO_INT_TRIG_LOW) == 0);
    a->idr &= ~BIT(1);
    gmk_wired_scan(rows);
    gmk_wired_scan(rows);
    assert(rows[0] == columns); /* IRQ failure cannot strand the keyboard. */
    a->idr |= BIT(1);
    gmk_wired_scan(rows);
    gmk_wired_scan(rows);
    assert(!scan_idle);
}
int main(void) {
    assert(gmk_host_switch() == 0 && !gmk_split_available() && !gmk_wired_mode());
    /* Fake MMIO is anonymous process RAM at the vendor register addresses. */
    assert(mmap((void *)0x40000000, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0x40000000);
    struct wb32_gpio *a = (void *)WB32_GPIOA_BASE, *b = (void *)WB32_GPIOB_BASE, *c = (void *)WB32_GPIOC_BASE;
    for (unsigned i = 0; i < 3; ++i)
        assert(wb32_gpio_init(&fake_gpio_ports[i]) == 0);
    test_exti();
    b->idr = 1U << 9;
    c->idr = 0xffff;
    a->idr = 0xffff;
    assert(wired_init() == 0);
    assert(gmk_wired_is_left() && gmk_wired_mode());
    check_host_modes(c);
    assert(saw_divider && *(uint32_t *)REG(0xc0) == 1 && *(uint32_t *)REG(0xc) == 3 && *(uint32_t *)REG(4) == 5);
    assert(*(uint32_t *)REG(8) == 7 && *(uint32_t *)REG(0x98) == 1 && *(uint32_t *)REG(0x9c) == 0);
    assert((sys_read32(WB32_RCC_BASE + 0x48) & 0xc0e0) == 0xc0e0);
    uint8_t out[4];
    hw_rx[0] = 10;
    hw_rx[1] = 11;
    hw_size  = 2;
    fake_uart_isr(NULL);
    assert(uart_rx.count == 2);
    deadline = fake_ms + 20;
    assert(read_uart(NULL, out, 2));
    assert(out[0] == 10 && out[1] == 11);
    deadline      = fake_ms + 20;
    int64_t start = fake_ms;
    assert(!read_uart(NULL, out, 1));
    assert(fake_ms - start == 20);
    hw_pos   = 0;
    hw_size  = 1;
    hw_error = 8;
    fake_uart_isr(NULL);
    assert(atomic_get(&rx_fault));
    deadline = fake_ms + 20;
    assert(!read_uart(NULL, out, 1));
    clear_rx(NULL);
    hw_error = 0;
    assert(!atomic_get(&rx_fault));
    for (unsigned i = 0; i < 128; ++i)
        assert(k_msgq_put(&uart_rx, &out[0], K_NO_WAIT) == 0);
    hw_pos  = 0;
    hw_size = 1;
    fake_uart_isr(NULL);
    assert(atomic_get(&rx_fault));
    clear_rx(NULL);
    *(uint32_t *)REG(0x7c) = 0;
    deadline               = fake_ms + 20;
    start                  = fake_ms;
    assert(!write_uart(NULL, out, 1));
    assert(fake_ms - start == 20);
    *(uint32_t *)REG(0x7c) = 2;
    deadline               = fake_ms + 20;
    assert(write_uart(NULL, out, 1));
    uint16_t rows[5];
    a->idr &= ~(1U << 1);
    gmk_wired_scan(rows);
    assert(rows[0] == 0);
    fake_ms += 5;
    gmk_wired_scan(rows);
    assert(rows[0] == 0xff);
    c->idr &= ~(1U << 14);
    gmk_wired_scan(rows);
#ifdef CONFIG_GMK70_RADIO
    assert(rows[0] == 0xff);
#else
    for (unsigned i = 0; i < 5; ++i)
        assert(rows[i] == 0);
#endif
    uint8_t request[32] = {0}, response[32];
    memset(response, 99, 32);
    assert(!gmk_wired_update(request, response));
    for (unsigned i = 0; i < 32; ++i)
        assert(response[i] == 0);
#ifdef CONFIG_GMK70_RADIO
    for (unsigned physical = 1; physical <= 2; ++physical) {
        c->idr = (c->idr & ~((1U << 14) | (1U << 15))) | (physical == 2 ? 1U << 14 : 1U << 15);
        assert(!gmk_wired_mode());
        gmk_wired_scan(rows);
        assert(rows[0] == 0xff);
        emulate_peer = true;
        gmk_wired_poll();
        emulate_peer = false;
        assert(gmk_wired_remote_matrix(rows));
        assert(rows[0] == 1 && rows[1] == 256 && rows[4] == 4);
        struct gmk_wired_stats wireless_stats;
        gmk_wired_get_stats(&wireless_stats);
        assert(wireless_stats.peer);
        unsigned transmissions = uart_transmissions;
        assert(!gmk_wired_update(request, response));
        assert(uart_transmissions == transmissions);
        for (unsigned i = 0; i < sizeof(response); ++i)
            assert(response[i] == 0);
    }
    c->idr &= ~((1U << 14) | (1U << 15));
    gmk_wired_scan(rows);
    for (unsigned i = 0; i < 5; ++i)
        assert(rows[i] == 0);
    assert(!gmk_wired_remote_matrix(rows));
    for (unsigned i = 0; i < 5; ++i)
        assert(rows[i] == 0);
    struct gmk_wired_stats invalid_stats;
    gmk_wired_get_stats(&invalid_stats);
    assert(!invalid_stats.peer);
    c->idr |= 1U << 15;
#endif
    c->idr |= 1U << 14;
    b->idr = 0;
    memset(&debounce, 0, sizeof(debounce));
    assert(wired_init() == 0);
    assert(!gmk_wired_is_left());
    check_host_modes(c);
    gmk_wired_scan(rows);
    fake_ms += 5;
    gmk_wired_scan(rows);
    assert(rows[0] == 0x1ff);
    assert(!gmk_wired_update(request, response));
    struct gmk_wired_stats stats, unchanged;
    memset(&stats, 0xa5, sizeof(stats));
    memcpy(&unchanged, &stats, sizeof(stats));
    wired_lock.locked = 1;
    start             = fake_ms;
    assert(!gmk_wired_try_get_stats(&stats));
    assert(wired_lock.locked == 1 && fake_ms == start && !memcmp(&stats, &unchanged, sizeof(stats)));
    wired_lock.locked     = 0;
    counters.transactions = 7;
    have_last_rx          = true;
    last_rx_seen          = (uint32_t)fake_ms;
    assert(gmk_wired_try_get_stats(&stats) && stats.transactions == 7 && stats.peer && wired_lock.locked == 0);
    fake_ms += 100;
    assert(gmk_wired_try_get_stats(&stats) && !stats.peer);
    last_rx_seen = (uint32_t)fake_ms;
    c->idr &= ~(1U << 14);
    assert(gmk_wired_try_get_stats(&stats) && !stats.peer);
    gmk_wired_scan(rows);
    for (unsigned i = 0; i < 5; ++i)
        assert(rows[i] == 0);
    test_idle_matrix(true);
    test_idle_matrix(false);
    assert(munmap((void *)0x40000000, 0x20000) == 0);
    puts("native WB32 UART/GPIO EXTI/idle matrix/arm race/held key/fallback/both-role tests passed");
}

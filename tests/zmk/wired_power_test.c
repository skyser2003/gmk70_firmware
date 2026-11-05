/* SPDX-License-Identifier: Apache-2.0 */
#define main original_wired_fixture_main
#include "wired_wb32_test.c"
#undef main

static void power_start(bool central) {
    gmk_wired_power_resume();
    /* A test switches physical roles between synthetic boots. Real hardware
     * has one immutable role per boot; discard its prior LEFT-only IRQ owner. */
    if (left) {
        assert(gpio_pin_interrupt_configure(&fake_gpio_ports[1], 6, GPIO_INT_DISABLE) == 0);
        assert(gpio_pin_interrupt_configure(&fake_gpio_ports[1], 7, GPIO_INT_DISABLE) == 0);
    }
    matrix_idle_end();
    clear_rx(NULL);
    memset(&debounce, 0, sizeof(debounce));
    hw_pos = hw_size = hw_error = 0;
    have_first = hw_tx_busy = arm_race = false;
    power_arm_race                     = 0;
    struct wb32_gpio *a = (void *)WB32_GPIOA_BASE, *b = (void *)WB32_GPIOB_BASE, *c = (void *)WB32_GPIOC_BASE;
    a->idr = c->idr = 0xffff;
    b->idr          = BIT(6) | BIT(7) | (central ? BIT(9) : 0);
    unsigned before = native_encoder_initializations;
    assert(wired_init() == 0);
    assert(native_encoder_initializations == before + (central ? 1 : 0));
    uint16_t rows[5];
    gmk_wired_scan(rows);
    assert(scan_idle && gmk_wired_power_idle());
}

/* Any unfinished local state or UART work must prevent arming sleep. */
static void test_idle_guards(void) {
    power_start(true);
    wired_lock.locked = 1;
    assert(!gmk_wired_power_idle());
    assert(gmk_wired_power_input_idle());
    wired_lock.locked     = 0;
    debounce.candidate[0] = 1;
    assert(!gmk_wired_power_idle());
    assert(!gmk_wired_power_input_idle());
    debounce.candidate[0] = 0;
    debounce.stable[2]    = 1;
    assert(!gmk_wired_power_idle());
    debounce.stable[2] = 0;
    encoder.count      = 1;
    assert(!gmk_wired_power_idle());
    encoder.count      = 0;
    const uint8_t byte = 1;
    assert(k_msgq_put(&uart_rx, &byte, K_NO_WAIT) == 0);
    assert(!gmk_wired_power_idle());
    assert(gmk_wired_power_input_idle());
    clear_rx(NULL);
    hw_tx_busy = true;
    assert(!gmk_wired_power_idle());
    hw_tx_busy = false;
    hw_rx[0]   = 1;
    hw_pos     = 0;
    hw_size    = 1;
    assert(!gmk_wired_power_idle());
    hw_size = 0;
    assert(gmk_wired_power_idle());
}

static void test_wake_lines_and_encoder_ownership(bool central) {
    power_start(central);
    unsigned initializations = native_encoder_initializations;
    assert(gmk_wired_power_prepare(false));
    const uint32_t local = BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(6) | BIT(7) | BIT(13) | BIT(14) | BIT(15);
    assert((sys_read32(EXTI_IMR) & local) == local);
    assert(exti_owner[7] == &fake_gpio_ports[0]);
    assert(exti_owner[6] == &fake_gpio_ports[1]);
    assert(exti_owner[10] == NULL);
    assert(!gmk_wired_power_wake_pending());
    gmk_wired_power_pause(true);
    assert(exti_owner[10] == &fake_gpio_ports[0]);
    assert(!gmk_wired_power_wake_pending());
    const unsigned pins[] = {1, 6, 7, 10, 13, 14, 15};
    for (unsigned i = 0; i < ARRAY_SIZE(pins); ++i) {
        wake_pin(pins[i]);
        assert(gmk_wired_power_wake_pending());
    }
    gmk_wired_power_resume();
    assert(native_encoder_initializations == initializations);
    assert(exti_owner[7] == (central ? &fake_gpio_ports[1] : NULL));
    assert(exti_owner[10] == NULL && exti_owner[14] == NULL && exti_owner[15] == NULL);
    assert(encoder_irq_ready == central);
}

/* Local wake stays latched even when the ordinary scanner consumes its edge. */
static void test_local_latch_and_final_pause_preserve_new_work(void) {
    power_start(true);
    assert(gmk_wired_power_prepare(false));
    hw_rx[0] = 1;
    hw_pos   = 0;
    hw_size  = 1;
    fake_uart_isr(NULL);
    assert(!gmk_wired_power_wake_pending()); /* Power RPCs remain live. */
    assert(gmk_wired_power_prepare(false));  /* Idempotent while power traffic is queued. */
    clear_rx(NULL);
    wake_pin(2);
    uint16_t rows[5];
    gmk_wired_scan(rows);
    assert(!atomic_get(&scan_pending));
    assert(gmk_wired_power_wake_pending());
    gmk_wired_power_pause(true);
    assert(gmk_wired_power_wake_pending());
    gmk_wired_power_resume();
    power_start(false);
    assert(gmk_wired_power_prepare(false));
    const uint8_t byte = 0x5a;
    assert(k_msgq_put(&uart_rx, &byte, K_NO_WAIT) == 0);
    gmk_wired_power_pause(true);
    assert(gmk_wired_power_wake_pending());
    gmk_wired_power_resume();
}

static void test_pause_stops_normal_peer_and_matrix_work(void) {
    power_start(true);
    assert(gmk_wired_power_prepare(false));
    gmk_wired_power_pause(true);
    wake_pin(1);
    unsigned before  = uart_transmissions;
    int64_t  started = fake_ms;
    uint16_t rows[5];
    for (unsigned i = 0; i < 100; ++i) {
        gmk_wired_scan(rows);
        gmk_wired_poll();
    }
    assert(fake_ms == started && uart_transmissions == before);
    assert(atomic_get(&scan_pending) && gmk_wired_power_wake_pending());
    uint8_t request[32]    = {0}, response[32];
    *(uint32_t *)REG(0x7c) = 2;
    assert(!gmk_wired_split_command(request, response)); /* No simulated peer response. */
    assert(uart_transmissions > before);                 /* Explicit power/control RPC still transmits. */
    gmk_wired_power_resume();
}

static void test_prepare_keeps_control_rpc_without_background_polling(void) {
    power_start(true);
    assert(gmk_wired_power_prepare(false) && !power_paused);
    unsigned before = uart_transmissions, polls = counters.polls;
    for (unsigned i = 0; i < 10; ++i)
        gmk_wired_poll();
    assert(counters.polls == polls && uart_transmissions == before);
    uint8_t request[32]    = {0}, response[32];
    *(uint32_t *)REG(0x7c) = 2;
    assert(!gmk_wired_split_command(request, response)); /* Missing simulated response. */
    assert(uart_transmissions > before);                 /* Required power RPC still gets the wire. */
    gmk_wired_power_resume();
    gmk_wired_poll();
    assert(counters.polls > polls); /* Cancellation resumes ordinary polling. */
}

/* A key/cable change during the last local arm must abort and restore B7. */
static void test_arm_race_and_conflicting_owner(void) {
    for (unsigned race = 1; race <= 2; ++race) {
        power_start(true);
        power_arm_race = race;
        assert(!gmk_wired_power_prepare(false));
        assert(exti_owner[7] == &fake_gpio_ports[1] && encoder_irq_ready);
    }
    power_start(true);
    assert(gpio_pin_interrupt_configure(&fake_gpio_ports[1], 14, GPIO_INT_EDGE_BOTH) == 0);
    assert(!gmk_wired_power_prepare(false));
    assert(exti_owner[14] == &fake_gpio_ports[1]);
    assert(exti_owner[7] == &fake_gpio_ports[1] && encoder_irq_ready);
    assert(gpio_pin_interrupt_configure(&fake_gpio_ports[1], 14, GPIO_INT_DISABLE) == 0);
    power_start(true);
    assert(gpio_pin_interrupt_configure(&fake_gpio_ports[2], 15, GPIO_INT_EDGE_BOTH) == 0);
    assert(!gmk_wired_power_prepare(false));
    assert(exti_owner[15] == &fake_gpio_ports[2]); /* Same-port foreign client survives rollback. */
    assert(gpio_pin_interrupt_configure(&fake_gpio_ports[2], 15, GPIO_INT_DISABLE) == 0);
}

static void test_pending_edges_and_final_uart_races(void) {
    power_start(true);
    *(uint32_t *)EXTI_PR |= BIT(2);
    assert(!gmk_wired_power_prepare(false));
    assert(gmk_wired_power_wake_pending() && (sys_read32(EXTI_PR) & BIT(2)));
    for (unsigned kind = 0; kind < 3; ++kind) {
        power_start(false);
        assert(gmk_wired_power_prepare(false));
        if (kind == 0) ((struct wb32_gpio *)WB32_GPIOA_BASE)->idr &= ~BIT(10);
        if (kind == 1) hw_tx_busy = true;
        if (kind == 2) assert(gpio_pin_interrupt_configure(&fake_gpio_ports[2], 10, GPIO_INT_EDGE_BOTH) == 0);
        gmk_wired_power_pause(true);
        assert(gmk_wired_power_wake_pending());
        gmk_wired_power_resume();
        if (kind == 2) {
            assert(exti_owner[10] == &fake_gpio_ports[2]);
            assert(gpio_pin_interrupt_configure(&fake_gpio_ports[2], 10, GPIO_INT_DISABLE) == 0);
        }
    }
}

static unsigned power_handler_calls;
static void     cached_power_response(const uint8_t request[32], uint8_t response[32]) {
    assert(!left && power_paused && wired_lock.locked);
    ++power_handler_calls;
    for (unsigned i = 0; i < 32; ++i)
        response[i] = request[i] ^ 0xa5U;
}
static void serve_power_transaction(uint8_t id, const uint8_t *data, size_t size) {
    first      = id;
    have_first = true;
    deadline   = fake_ms + 20;
    for (size_t i = 0; i < size; ++i)
        assert(k_msgq_put(&uart_rx, data + i, K_NO_WAIT) == 0);
    k_mutex_lock(&wired_lock, K_FOREVER);
    assert(serve_one());
    k_mutex_unlock(&wired_lock);
    have_first = false;
}

/* Paused RIGHT still serves the same queued power RPC in its serial owner. */
static void test_paused_peripheral_serves_control_requests(void) {
    power_start(false);
    assert(gmk_wired_power_prepare(false));
    gmk_wired_power_pause(true);
    protocol.split_command    = cached_power_response;
    *(uint32_t *)REG(0x7c)    = 2;
    uint8_t info[4]           = {0, 17, 32, 32};
    info[0]                   = gmk_wire_crc8(info + 1, 3);
    const uint8_t payload[32] = {0x70, 0x77};
    const uint8_t id          = 17;
    serve_power_transaction(13, info, sizeof(info));
    serve_power_transaction(14, payload, sizeof(payload));
    serve_power_transaction(15, &id, 1);
    serve_power_transaction(16, NULL, 0);
    assert(power_handler_calls == 1);
    gmk_wired_power_resume();
}

static bool request_critical_prepare(void) {
    return gmk_wired_power_prepare(true);
}
static void test_critical_ignores_held_matrix_and_restores_abort(void) {
    power_start(true);
    struct wb32_gpio *a = (void *)WB32_GPIOA_BASE;
    a->idr &= ~BIT(1);
    wake_pin(1);
    uint16_t rows[5];
    gmk_wired_scan(rows);
    gmk_wired_scan(rows);
    assert(debounce.stable[0] && !gmk_wired_power_idle());
    assert(request_critical_prepare());
    assert(!(sys_read32(EXTI_IMR) & (BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(13))));
    assert(!gmk_wired_power_wake_pending());
    wake_pin(1);
    assert(!gmk_wired_power_wake_pending());
    gmk_wired_power_pause(true);
    assert(!gmk_wired_power_wake_pending());
    wake_pin(7);
    assert(gmk_wired_power_wake_pending());
    gmk_wired_power_resume();
    assert(!scan_idle && encoder_irq_ready);
    a->idr |= BIT(1);
    gmk_wired_scan(rows);
    gmk_wired_scan(rows);
    assert(!rows[0]);
}

int main(void) {
    assert(mmap((void *)0x40000000, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0x40000000);
    for (unsigned i = 0; i < 3; ++i)
        assert(wb32_gpio_init(&fake_gpio_ports[i]) == 0);
    /* Establish row pointers before the fixture's first matrix-idle cleanup. */
    ((struct wb32_gpio *)WB32_GPIOA_BASE)->idr = 0xffff;
    ((struct wb32_gpio *)WB32_GPIOB_BASE)->idr = BIT(9) | BIT(6) | BIT(7);
    ((struct wb32_gpio *)WB32_GPIOC_BASE)->idr = 0xffff;
    assert(wired_init() == 0);
    test_idle_guards();
    test_wake_lines_and_encoder_ownership(true);
    test_wake_lines_and_encoder_ownership(false);
    test_local_latch_and_final_pause_preserve_new_work();
    test_pause_stops_normal_peer_and_matrix_work();
    test_prepare_keeps_control_rpc_without_background_polling();
    test_arm_race_and_conflicting_owner();
    test_pending_edges_and_final_uart_races();
    test_paused_peripheral_serves_control_requests();
    test_critical_ignores_held_matrix_and_restores_abort();
    gmk_wired_power_resume();
    assert(munmap((void *)0x40000000, 0x20000) == 0);
    puts("Wired power: both roles, local/RX wake gates, ownership, arm races and paused polling passed");
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#define main legacy_wired_main
#include "wired_wb32_test.c"
#undef main
static unsigned scan_notifications, service_notifications;
void            gmk_scan_notify(void) {
    ++scan_notifications;
}
void gmk_service_notify(void) {
    ++service_notifications;
}

static bool event_read_ack(uint8_t request[4], uint8_t response[16]) {
    request[2] = event_link.tag;
    request[3] = gmk_wire_crc8(request, 3);
    return event_read(request, response);
}
static void test_master_events(void);
int         main(void) {
    void *mapped = mmap((void *)0x40000000, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(mapped == (void *)0x40000000);
    struct wb32_gpio *a = (void *)WB32_GPIOA_BASE, *b = (void *)WB32_GPIOB_BASE, *c = (void *)WB32_GPIOC_BASE;
    a->idr = c->idr        = 0xffff;
    b->idr                 = 0; /* RIGHT, wired. */
    *(uint32_t *)REG(0x7c) = 2;
    assert(wired_init() == 0);
    uint16_t rows[5];
    gmk_wired_scan(rows);
    assert(scan_idle);
    uint8_t request[32] = {'E', 'V', 'T', 1}, response[32], frame[16];
    request[8]          = 123;
    request[31]         = gmk_wire_crc8(request, 31);
    assert(event_control(request, response) && response[5] == 1 && !event_link.enabled);
    request[4]  = 1;
    request[31] = gmk_wire_crc8(request, 31);
    assert(event_control(request, response) && event_link.enabled && !event_armed);
    unsigned transmissions = uart_transmissions;
    gmk_wired_scan(rows);
    assert(uart_transmissions == transmissions); /* Enable reply lost. */
    uint8_t acknowledged[4] = {0};
    assert(event_read_ack(acknowledged, frame));
    acknowledged[0] = frame[0];
    acknowledged[1] = frame[1];
    assert(event_read_ack(acknowledged, frame) && !event_link.count);
    a->idr &= ~BIT(1);
    matrix_wake(NULL, NULL, 0);
    gmk_wired_scan(rows);
    fake_ms += 5;
    gmk_wired_scan(rows);
    assert(rows[0] && event_link.count == 1 && uart_transmissions == transmissions + 1);
    a->idr |= BIT(1);
    gmk_wired_scan(rows);
    fake_ms += 5;
    gmk_wired_scan(rows);
    assert(!rows[0] && event_link.count == 2);
    assert(!gmk_wired_power_input_idle()); /* Undelivered release bars sleep. */
    assert(event_read_ack(acknowledged, frame) && frame[3]);
    acknowledged[0] = frame[0];
    acknowledged[1] = frame[1];
    uint8_t invalid[4];
    memcpy(invalid, acknowledged, sizeof(invalid));
    invalid[3] = gmk_wire_crc8(invalid, 3) ^ 1U;
    assert(!event_read(invalid, frame) && event_link.count == 2);
    invalid[2] ^= 1;
    invalid[3] = gmk_wire_crc8(invalid, 3);
    assert(!event_read(invalid, frame) && event_link.count == 2);
    request[8] += 1; /* New master/session tag retains both pending edges. */
    request[31] = gmk_wire_crc8(request, 31);
    assert(event_control(request, response) && event_link.count == 2 && !event_armed);
    acknowledged[3] = gmk_wire_crc8(acknowledged, 3);
    assert(!event_read(acknowledged, frame) && event_link.count == 2);
    assert(event_read_ack(acknowledged, frame) && !frame[3]);
    acknowledged[0] = frame[0];
    acknowledged[1] = frame[1];
    assert(event_read_ack(acknowledged, frame) && !event_link.count);
    assert(gmk_wired_power_input_idle());
    assert(mode_irq_ready && gmk_wired_power_prepare(false));
    gmk_wired_power_pause(true);
    assert(power_prepared && power_paused && !gmk_wired_power_wake_pending());
    transmissions = uart_transmissions;
    fake_ms += 10000;
    gmk_wired_scan(rows);
    assert(gmk_wired_scan_delay() == UINT32_MAX && uart_transmissions == transmissions);
    assert(!event_read_ack(acknowledged, frame)); /* No maintenance after sleep barrier. */
    gmk_wired_power_resume();
    assert((sys_read32(EXTI_IMR) & (BIT(14) | BIT(15))) == (BIT(14) | BIT(15)));
    *(uint32_t *)EXTI_PR |= BIT(14);
    assert(!gmk_wired_power_prepare(false));
    assert(sys_read32(EXTI_PR) & BIT(14)); /* Adoption must not clear an edge. */
    gmk_wired_power_resume();
    wake_pin(14);
    uint32_t route = sys_read32(0x40001414U);
    sys_write32(route & ~(15U << 8), 0x40001414U); /* Another port owns EXTI14. */
    assert(!gmk_wired_power_prepare(false));
    sys_write32(route, 0x40001414U);
    assert(gmk_wired_power_prepare(true));
    gmk_wired_power_pause(true);
    assert(power_paused && !gmk_wired_power_wake_pending());
    gmk_wired_power_resume();
    assert(event_link.enabled);
    gmk_wired_scan(rows);
#ifdef CONFIG_GMK70_RETAINED_SLEEP
    assert(event_link.enabled); /* Sleeping time does not expire a coordinated lease. */
    a->idr &= ~BIT(1);
    matrix_wake(NULL, NULL, 0);
    gmk_wired_scan(rows);
    fake_ms += 5;
    gmk_wired_scan(rows);
    a->idr |= BIT(1);
    gmk_wired_scan(rows);
    fake_ms += 5;
    gmk_wired_scan(rows);
    assert(event_link.count == 2);
    assert(event_read_ack(acknowledged, frame) && frame[3]);
    acknowledged[0] = frame[0];
    acknowledged[1] = frame[1];
    assert(event_read_ack(acknowledged, frame) && !frame[3]);
    acknowledged[0] = frame[0];
    acknowledged[1] = frame[1];
    assert(event_read_ack(acknowledged, frame) && !event_link.count);
    fake_ms += GMK_EVENT_LEASE_MS;
    gmk_wired_scan(rows);
#endif
    assert(!event_link.enabled && !event_armed);
    /* Filter notices only at ACK boundaries; payload 0xfc remains intact. */
    left = true;
    master_clear(NULL);
    uint8_t marker = GMK_EVENT_NOTICE, ack = 18 ^ 19, payload = GMK_EVENT_NOTICE;
    assert(!k_msgq_put(&uart_rx, &marker, K_NO_WAIT));
    assert(!k_msgq_put(&uart_rx, &ack, K_NO_WAIT));
    assert(!k_msgq_put(&uart_rx, &payload, K_NO_WAIT));
    uint8_t value;
    assert(read_uart(NULL, &value, 1) && value == ack);
    assert(read_uart(NULL, &value, 1) && value == payload);
    master_clear(NULL);
    uint8_t native[2] = {'Z', 'M'};
    assert(write_uart(NULL, native, sizeof(native)) && !master_ack);
    assert(!k_msgq_put(&uart_rx, &payload, K_NO_WAIT));
    assert(read_uart(NULL, &value, 1) && value == payload);
    assert(scan_notifications && service_notifications);
    test_master_events();
    assert(munmap(mapped, 0x20000) == 0);
    puts("event WB32: explicit enable/arm, queued tap, sleep silence, ACK boundary and native payload preservation passed");
}

/* Byte-delivery scheduler around the ACTUAL protocol server. ACK delivery lets
 * the master send a transaction's body before the server dispatches it. */
static struct gmk_protocol   remote_protocol;
static struct gmk_event_link remote_events;
static uint16_t              remote_rows[5];
static uint8_t               remote_request[40], remote_id;
static size_t                remote_size, remote_need, remote_position;
static bool                  remote_ack, remote_capable = true, remote_drop;
static unsigned              corrupt_replies, event_exchanges, legacy_reads;
static bool                  remote_read(void *ctx, uint8_t *out, size_t count) {
    (void)ctx;
    if (remote_position + count > remote_size) return false;
    memcpy(out, remote_request + remote_position, count);
    remote_position += count;
    return true;
}
static bool remote_write(void *ctx, const uint8_t *in, size_t count) {
    (void)ctx;
    if (!remote_ack) {
        assert(count == 1 && in[0] == (remote_id ^ 19));
        remote_ack = true;
        return true;
    }
    for (size_t i = 0; i < count; ++i) {
        uint8_t byte = in[i];
        if (remote_id == 18 && corrupt_replies && i + 1 == count) {
            byte ^= 1;
            --corrupt_replies;
        }
        assert(!k_msgq_put(&uart_rx, &byte, K_NO_WAIT));
    }
    return true;
}
static void remote_clear(void *ctx) {
    (void)ctx;
}
static void remote_dispatch(void) {
    const struct gmk_wire_io io = {NULL, remote_read, remote_write, remote_clear};
    remote_position             = 0;
    remote_ack                  = false;
    assert(gmk_protocol_serve(&remote_protocol, &io));
    remote_size = 0;
}
static void remote_byte(uint8_t byte) {
    if (remote_drop) return;
    if (!remote_size) {
        remote_id = byte;
        assert(byte == 0 || byte == 1 || byte == 13 || byte == 14 || byte == 15 || byte == 16 || byte == 18);
        remote_need = byte == 13 || byte == 18 ? 4 : byte == 14 ? 32 : byte == 15 ? 1 : 0;
        uint8_t ack = byte ^ 19;
        assert(!k_msgq_put(&uart_rx, &ack, K_NO_WAIT));
        if (byte == 0) ++legacy_reads;
        if (byte == 18) ++event_exchanges;
    }
    assert(remote_size < sizeof(remote_request));
    remote_request[remote_size++] = byte;
    if (remote_size == remote_need + 1) remote_dispatch();
}
static bool remote_control(const uint8_t request[32], uint8_t response[32]) {
    if (!remote_capable) return false;
    return gmk_event_control(&remote_events, remote_rows, true, request, response);
}
static bool remote_event_read(const uint8_t request[4], uint8_t response[16]) {
    assert(remote_events.enabled && request[2] == remote_events.tag && request[3] == gmk_wire_crc8(request, 3));
    gmk_event_reply(&remote_events, (uint16_t)(request[0] | ((uint16_t)request[1] << 8)), response);
    return true;
}
static void remote_legacy(void) {
    remote_events.enabled = false;
}
static void ignored_rpc(const uint8_t request[32], uint8_t response[32]) {
    (void)request;
    memset(response, 0, 32);
}
static void test_master_events(void) {
    left           = true;
    power_prepared = power_paused = false;
    event_mode = event_probed = event_fast = false;
    event_failures                         = 0;
    encoder_ready                          = false;
    scan_idle = scan_irq_ready = true;
    atomic_set(&scan_pending, 0);
    atomic_set(&event_notice, 0);
    atomic_set(&master_receiving, 0);
    peer            = (struct gmk_peer){0};
    remote_protocol = (struct gmk_protocol){.event_control = remote_control, .event_read = remote_event_read, .legacy_read = remote_legacy, .split_command = ignored_rpc};
    gmk_protocol_matrix(&remote_protocol, remote_rows);
    event_uart_tx = remote_byte;
    gmk_wired_poll();
    assert(event_mode && remote_events.enabled);
    gmk_wired_poll();
    gmk_wired_poll();
    assert(!event_fast && !remote_events.count);
    assert(gmk_wired_scan_delay() == GMK_EVENT_HEARTBEAT_MS);
    uint16_t rows[5];
    remote_rows[0] = 4;
    gmk_event_sample(&remote_events, remote_rows, false);
    remote_rows[0] = 0;
    gmk_event_sample(&remote_events, remote_rows, true);
    unsigned reads  = legacy_reads;
    corrupt_replies = 3;
    atomic_set(&event_notice, 1);
    for (unsigned n = 0; n < 3; ++n)
        gmk_wired_poll();
    assert(!corrupt_replies && legacy_reads == reads && remote_events.count == 2);
    gmk_wired_poll();
    assert(gmk_wired_remote_matrix(rows) && rows[0] == 4);
    gmk_wired_poll();
    assert(gmk_wired_remote_matrix(rows) && rows[0] == 0);
    gmk_wired_poll();
    assert(!remote_events.count && !event_fast);
    remote_rows[0] = 16;
    gmk_event_sample(&remote_events, remote_rows, false);
    remote_rows[0] = 0;
    gmk_event_sample(&remote_events, remote_rows, true);
    event_mode = event_probed = false; /* LEFT restarts while RIGHT retains a tap. */
    event_session += 7;
    peer = (struct gmk_peer){0};
    gmk_wired_poll();
    assert(legacy_reads == reads && gmk_wired_remote_matrix(rows) && rows[0] == 16);
    gmk_wired_poll();
    assert(gmk_wired_remote_matrix(rows) && !rows[0]);
    gmk_wired_poll();
    assert(!remote_events.count && !event_fast);
    unsigned quiet_exchanges = event_exchanges;
    fake_ms += GMK_EVENT_HEARTBEAT_MS - 1;
    gmk_wired_poll();
    assert(event_exchanges == quiet_exchanges && gmk_wired_remote_matrix(rows));
    fake_ms += 1;
    gmk_wired_poll();
    assert(event_exchanges == quiet_exchanges + 1);
    remote_rows[0] = 8;
    gmk_event_sample(&remote_events, remote_rows, false);
    atomic_set(&event_notice, 1);
    gmk_wired_poll();
    assert(gmk_wired_remote_matrix(rows) && rows[0] == 8 && !peer.quiet_timeout_ms);
    fake_ms += 100;
    assert(!gmk_wired_remote_matrix(rows) && !rows[0]); /* Held release retains 100 ms bound. */
    /* A truly older peer still negotiates no event bytes. */
    remote_capable = false;
    remote_events  = (struct gmk_event_link){0};
    event_mode = event_probed = false;
    unsigned before           = event_exchanges;
    gmk_wired_poll();
    gmk_wired_poll();
    assert(!event_mode && event_exchanges == before);
    event_uart_tx = NULL;
    puts("event master: real RPC negotiation, corrupt replies and master restart retain taps, heartbeat, held-key expiry and legacy fallback passed");
}

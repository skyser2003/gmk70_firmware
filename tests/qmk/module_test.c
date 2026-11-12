// SPDX-License-Identifier: GPL-2.0-or-later
// Production parser/queue with simulated clock and UART; no RF emulation.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "module.h"
#include "smsg.h"
#ifdef PROTOCOL_CHIBIOS
#    include "hal.h"
test_serial_driver_t SD3;
static eventflags_t  serial_errors;
void                 chEvtRegisterMaskWithFlags(event_source_t *source, event_listener_t *listener, eventmask_t events, eventflags_t flags) {
    assert(source == &SD3.event && events == 0 && flags != 0);
    listener->flags = flags;
}
eventflags_t chEvtGetAndClearFlags(event_listener_t *listener) {
    eventflags_t flags = serial_errors & listener->flags;
    serial_errors      = 0;
    return flags;
}
#endif
static uint32_t now;
static unsigned sends, received, cursor;
static uint8_t  last_packet[50], receive[256];
static uint32_t last_length;
static unsigned raw_dispatches, host_notifications;
static uint8_t  raw_received[32];

void md_receive_raw_cb(uint8_t *data, uint8_t length) {
    assert(length == sizeof(raw_received));
    memcpy(raw_received, data, length);
    ++raw_dispatches;
}
void md_receive_host_cb(bool resume) {
    (void)resume;
    ++host_notifications;
}
uint32_t sync_timer_read32(void) {
    return now;
}
uint32_t sync_timer_elapsed32(uint32_t stamp) {
    return now - stamp;
}
void uart_init(uint32_t baud) {
    assert(baud == 115200);
}
bool uart_available(void) {
    return cursor < received;
}
uint8_t uart_read(void) {
    return receive[cursor++];
}
void uart_transmit(const uint8_t *data, uint32_t length) {
    assert(length <= sizeof(last_packet));
    last_length = length;
    memcpy(last_packet, data, length);
    ++sends;
}
static void feed(const uint8_t *data, unsigned length) {
    memcpy(receive, data, length);
    received = length;
    cursor   = 0;
    md_main_task();
}
static void ack(void) {
    feed((uint8_t[]){0x61, 0x0D, 0x0A}, 3);
    md_main_task();
}
static void status(uint8_t state) {
    feed((uint8_t[]){MD_REV_CMD_DEVCTRL, state, MD_REV_CMD_DEVCTRL + state}, 3);
}
static void notification(uint8_t command, uint8_t value) {
    feed((uint8_t[]){command, value, (uint8_t)(command + value)}, 3);
}

static void parser_reset(void) {
    received = cursor = 0;
    now               = 1000;
    md_init();
    *md_getp_bat()       = 100;
    *md_getp_state()     = MD_STATE_DISCONNECTED;
    *md_getp_indicator() = 0;
    raw_dispatches = host_notifications = 0;
    sends                               = 0;
}

static void assert_tail_isolated(void) {
    assert(*md_getp_bat() == 100);
    assert(*md_getp_state() == MD_STATE_DISCONNECTED);
    assert(*md_getp_indicator() == 0);
    assert(host_notifications == 0 && raw_dispatches == 0 && sends == 0);
}

static void establish_status_context(void) {
    md_devs_change(DEVS_BT1, false);
    md_main_task();
    ack();
    sends = 0;
}

static void test_oversized_raw_tail_quarantine(void) {
    parser_reset();
    establish_status_context();
    feed((uint8_t[]){0xaf, 0x60, 255}, 3);
    now += 100;
    md_main_task(); // Empty timeout polls cannot release the raw tail.
    const uint8_t tail[] = {0x5c, 67, 0x9f, 0x5a, 7, 0x61, 0x60, 1, 0x61, 0x5b, 0x32, 0x8d};
    feed(tail, sizeof(tail));
    assert_tail_isolated();
    uint8_t remainder[244] = {0};
    feed(remainder, sizeof(remainder));
    assert_tail_isolated();
    notification(MD_REV_CMD_BATVOL, 42);
    assert(*md_getp_bat() == 42 && sends == 1);
}

static void test_expired_known_raw_tail(void) {
    parser_reset();
    feed((uint8_t[]){0xaf, 0x60, 3}, 3);
    now += 10;
    md_main_task();
    now += 100;
    feed((uint8_t[]){0x5c, 67, 0x9f, 0x50}, 4); // Valid raw checksum, expired frame.
    assert_tail_isolated();
    notification(MD_REV_CMD_BATVOL, 42);
    assert(*md_getp_bat() == 42 && sends == 1);
}

static void test_expired_partial_raw_header(void) {
    for (unsigned header_length = 1; header_length <= 2; ++header_length) {
        parser_reset();
        feed((uint8_t[]){0xaf, 0x60}, header_length);
        now += 10;
        md_main_task();
        feed((uint8_t[]){0x5c, 67, 0x9f, 0x5c, 67, 0x9f}, 6);
        assert_tail_isolated();
        uint8_t remainder[255] = {0};
        feed(remainder, 259 - header_length - 6);
        assert_tail_isolated();
        notification(MD_REV_CMD_BATVOL, 42);
        assert(*md_getp_bat() == 42 && sends == 1);
    }
}

static void test_partial_control_timeout(void) {
    parser_reset();
    now = UINT32_MAX - 4;
    feed((uint8_t[]){0x5c, 99}, 2);
    now += 10; // Timeout subtraction must survive wraparound.
    md_main_task();
    notification(MD_REV_CMD_BATVOL, 42);
    assert(*md_getp_bat() == 42 && sends == 1);
}

static void test_battery_range(void) {
    parser_reset();
    notification(MD_REV_CMD_BATVOL, 67);
    notification(MD_REV_CMD_BATVOL, 101);
    notification(MD_REV_CMD_BATVOL, 255);
    assert(*md_getp_bat() == 67);
    notification(MD_REV_CMD_BATVOL, 0);
    assert(*md_getp_bat() == 0);
    notification(MD_REV_CMD_BATVOL, 100);
    assert(*md_getp_bat() == 100);
}

static void test_battery_freshness(void) {
    parser_reset();
    assert(!md_battery_is_fresh());
    now = UINT32_MAX - 4999;
    notification(MD_REV_CMD_BATVOL, 67);
    assert(md_battery_is_fresh());
    now += 10000;
    notification(MD_REV_CMD_BATVOL, 255);
    assert(md_battery_is_fresh() && *md_getp_bat() == 67);
    ++now;
    assert(!md_battery_is_fresh());
    notification(MD_REV_CMD_BATVOL, 0);
    assert(md_battery_is_fresh());
    md_init();
    assert(!md_battery_is_fresh() && *md_getp_bat() == 100);
}

#ifdef PROTOCOL_CHIBIOS
static void test_uart_input_flags_are_not_faults(void) {
    parser_reset();
    serial_errors = SD_NOISE_ERROR | CHN_INPUT_AVAILABLE;
    notification(MD_REV_CMD_BATVOL, 42);
    assert(*md_getp_bat() == 42 && sends == 1);
}
static void test_uart_fault_retains_raw_tail(void) {
    const uint8_t      raw_headers[][3] = {{0xaf, 0x60, 3}, {0xaf, 0x60, 255}};
    const eventflags_t faults[]         = {SD_OVERRUN_ERROR, SD_PARITY_ERROR, SD_FRAMING_ERROR, SD_BREAK_DETECTED, SD_QUEUE_FULL_ERROR};
    for (unsigned fault = 0; fault < sizeof(faults) / sizeof(faults[0]); ++fault) {
        for (unsigned header = 0; header < 2; ++header) {
            parser_reset();
            feed(raw_headers[header], 3);
            serial_errors = faults[fault];
            feed((uint8_t[]){0x11, 0x22, 0x33, 0x44}, 4); // Drop buffered damage without consuming the tail budget.
            feed((uint8_t[]){0x5c, 67, 0x9f, 0x50}, 4);
            assert_tail_isolated();
            if (header) {
                uint8_t remainder[252] = {0};
                feed(remainder, sizeof(remainder));
            }
            notification(MD_REV_CMD_BATVOL, 42);
            assert(*md_getp_bat() == 42 && sends == 1);
        }
    }
}
#endif

static void test_valid_raw_dispatch_and_status_isolation(void) {
    parser_reset();
    establish_status_context();
    uint8_t packet[36] = {0xaf, 0x60, 32, 0x5c, 67, 0x9f, 0x5a, 7, 0x61, 0x60, 1, 0x61, 0x5b, 0x32, 0x8d};
    uint8_t sum        = 0;
    for (unsigned i = 0; i < sizeof(packet) - 1; ++i)
        sum += packet[i];
    packet[35] = sum;
    feed(packet, sizeof(packet));
    assert(raw_dispatches == 1 && sends == 1);
    assert(memcmp(raw_received, packet + 3, sizeof(raw_received)) == 0);
    assert(*md_getp_bat() == 100 && *md_getp_indicator() == 0 && *md_getp_state() == MD_STATE_DISCONNECTED && host_notifications == 0);
}
static void test_invalid_raw_subtype_is_not_dispatched(void) {
    parser_reset();
    // A checksum-valid three-byte frame must not reuse stale raw payload.
    feed((uint8_t[]){0xaf, 0x71, 0x20}, 3);
    assert_tail_isolated();
}

static void test_direct_reports_and_expiry(void) {
    md_init();
    md_send_devctrl(MD_SND_CMD_DEVCTRL_BT1);
    md_main_task();
    assert(sends == 1 && last_packet[0] == MD_SND_CMD_DEVCTRL);
    const uint8_t keys[8] = {0, 0, 4, 5};
    md_send_kb((uint8_t *)keys);
    assert(sends == 2 && last_packet[0] == MD_SND_CMD_SEND_KB);
    assert(memcmp(last_packet + 1, keys, 8) == 0);
    uint8_t sum = 0;
    for (unsigned i = 0; i < 9; ++i)
        sum += last_packet[i];
    assert(last_packet[9] == sum && smsg_get_state() == smsg_state_busy);
    // Missing ACK expires after the stock limit rather than blocking forever.
    const unsigned before = sends;
    for (now = 1; now < 2000 && smsg_is_busy(); ++now)
        md_main_task();
    assert(!smsg_is_busy() && now < 1000 && sends - before == 50);
    md_send_devctrl(MD_SND_CMD_DEVCTRL_BT2);
    md_main_task();
    assert(last_packet[1] == MD_SND_CMD_DEVCTRL_BT2);
    ack();
    assert(!smsg_is_busy());
    const unsigned query_before = sends;
    assert(md_inquire_bat());
    for (unsigned end = now + 100; now < end; ++now)
        md_main_task();
    assert(!smsg_is_busy() && sends - query_before == 2);
}
static void test_context_and_pairing(void) {
    md_init();
    *md_getp_state() = MD_STATE_DISCONNECTED;
    status(MD_REV_CMD_DEVCTRL_CONNECTED);
    assert(*md_getp_state() == MD_STATE_DISCONNECTED); // No dispatched selector.
    md_devs_change(DEVS_BT1, false);
    md_main_task();
    ack();
    status(MD_REV_CMD_DEVCTRL_CONNECTED);
    assert(md_reports_inhibited() && md_take_report_reset());
    assert(!md_take_report_reset());
    assert(*md_getp_state() == MD_STATE_CONNECTED && !md_reports_inhibited());
    md_devs_change(DEVS_BT2, false);         // Context still BT1 before dispatch.
    status(MD_REV_CMD_DEVCTRL_DISCONNECTED); // Task dispatches BT2 first.
    assert(*md_getp_state() == MD_STATE_DISCONNECTED);
    ack();
    md_send_devctrl(MD_SND_CMD_DEVCTRL_PAIR);
    md_main_task();
    assert(md_reports_inhibited());
    unsigned before  = sends;
    uint8_t  keys[8] = {0, 0, 4};
    md_send_kb(keys);
    assert(sends == before);
    ack();
    status(MD_REV_CMD_DEVCTRL_CONNECTED);
    assert(md_reports_inhibited() && md_take_report_reset());
    md_send_kb(keys);
    assert(!md_reports_inhibited() && last_packet[0] == MD_SND_CMD_SEND_KB);
}
static void test_stale_status_and_packet_bounds(void) {
    md_init();
    *md_getp_state() = MD_STATE_CONNECTED;
    md_devs_change(DEVS_BT1, false);
    md_main_task();
    // Keep first command waiting; subsequent channel is current but not dispatched.
    md_devs_change(DEVS_BT2, false);
    status(MD_REV_CMD_DEVCTRL_DISCONNECTED);
    assert(*md_getp_state() == MD_STATE_CONNECTED);
    ack(); // Dispatches the second selector.
    status(MD_REV_CMD_DEVCTRL_DISCONNECTED);
    assert(*md_getp_state() == MD_STATE_DISCONNECTED);
    ack();
    // An oversized raw length must not write beyond the parser's packet buffer.
    memset(receive, 0, sizeof(receive));
    receive[0] = MD_REV_CMD_RAW;
    receive[1] = MD_REV_CMD_RAW_OUT;
    receive[2] = 255;
    received   = sizeof(receive);
    cursor     = 0;
    md_main_task();
}

static void test_pairing_name_packet(void) {
    md_init();
    md_send_devinfo("GMK70-1");
    md_main_task();
    // UART bytes captured from V15's logical command 8 dispatch.
    const uint8_t expected[] = {0xA9, 7, 'G', 'M', 'K', '7', '0', '-', '1', 0x54};
    assert(memcmp(last_packet, expected, sizeof(expected)) == 0);
    assert(last_length == sizeof(expected));
    ack();
}
static void test_stop_command_drain(void) {
    md_init();
    md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
    md_main_task();
    assert(smsg_is_busy());
    memcpy(receive, (uint8_t[]){0x61, 0x0D, 0x0A}, 3);
    received = 3;
    cursor   = 0;
    md_wait_for_commands();
    assert(!smsg_is_busy() && cursor == received);
}
static void test_explicit_battery_refresh_waits_and_coalesces(void) {
    parser_reset();
    md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
    md_main_task();
    unsigned before = sends;
    for (unsigned i = 0; i < 100; ++i)
        md_request_battery();
    md_main_task();
    assert(sends == before); /* The USB-selection ACK still owns the queue. */
    ack();
    md_main_task();
    const uint8_t query[] = {0xa6, 0x53, 0xf9};
    assert(last_length == sizeof(query) && !memcmp(last_packet, query, sizeof(query)));
    assert(sends == before + 1);
    ack();
    for (unsigned i = 0; i < 100; ++i)
        md_main_task();
    assert(!smsg_is_busy() && sends == before + 1);
}
int main(int argc, char **argv) {
    test_explicit_battery_refresh_waits_and_coalesces();
    typedef struct {
        const char *name;
        void (*run)(void);
    } parser_test_t;
    const parser_test_t parser_tests[] = {
        {"invalid-raw-subtype", test_invalid_raw_subtype_is_not_dispatched},
        {"oversized-tail", test_oversized_raw_tail_quarantine},
        {"expired-tail", test_expired_known_raw_tail},
        {"expired-header", test_expired_partial_raw_header},
        {"control-timeout", test_partial_control_timeout},
        {"battery-range", test_battery_range},
        {"battery-freshness", test_battery_freshness},
        {"valid-raw", test_valid_raw_dispatch_and_status_isolation},
#ifdef PROTOCOL_CHIBIOS
        {"uart-fault", test_uart_fault_retains_raw_tail},
        {"uart-input", test_uart_input_flags_are_not_faults},
#endif
    };
    for (unsigned i = 0; i < sizeof(parser_tests) / sizeof(parser_tests[0]); ++i) {
        if (argc == 1 || strcmp(argv[1], parser_tests[i].name) == 0) parser_tests[i].run();
    }
    if (argc > 1) return 0;
    sends = 0;
    test_direct_reports_and_expiry();
    test_context_and_pairing();
    test_stale_status_and_packet_bounds();
    test_pairing_name_packet();
    test_stop_command_drain();
    puts("Radio: direct HID, finite command retries, battery retry limit, status context and pairing gates passed");
    return 0;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Production parser/queue with simulated clock and UART; no RF emulation.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "module.h"
#include "smsg.h"
static uint32_t now;
static unsigned sends, received, cursor;
static uint8_t  last_packet[50], receive[256];
static uint32_t last_length;
uint32_t        sync_timer_read32(void) {
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
int main(void) {
    test_direct_reports_and_expiry();
    test_context_and_pairing();
    test_stale_status_and_packet_bounds();
    test_pairing_name_packet();
    test_stop_command_drain();
    puts("Radio: direct HID, finite command retries, battery retry limit, status context and pairing gates passed");
}

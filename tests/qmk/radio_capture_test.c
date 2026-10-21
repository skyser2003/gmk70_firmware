// SPDX-License-Identifier: GPL-2.0-or-later
// Real radio parser/queue; only UART, clock and critical sections are simulated.
#define main module_fixture_main
#include "module_test.c"
#undef main

bool            gmk_radio_diag_query(uint8_t *packet, uint8_t length, bool allowed, uint32_t uptime) __attribute__((weak));
static unsigned raw_delivered;
void            md_receive_raw_cb(uint8_t *data, uint8_t length) {
    assert(length == 32 && data[0] == 1);
    ++raw_delivered;
}

static uint32_t read_word(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void request(uint8_t *p, uint8_t page, uint16_t token) {
    memset(p, 0, 32);
    memcpy(p, (uint8_t[]){0x22, 'R', 'D', 1, page, 0, token, token >> 8}, 8);
}
static void snapshot(uint32_t *words, uint16_t token) {
    for (unsigned page = 0; page < 5; ++page) {
        uint8_t p[32];
        request(p, page, token);
        assert(gmk_radio_diag_query(p, sizeof(p), true, 1234));
        assert(p[4] == page && p[5] == 0 && p[6] == (uint8_t)token && p[7] == token >> 8);
        for (unsigned i = 0; i < 6; ++i)
            words[page * 6 + i] = read_word(p + 8 + i * 4);
    }
}
static void raw_packet(uint8_t length, bool checksum_ok) {
    uint8_t p[36] = {MD_REV_CMD_RAW, MD_REV_CMD_RAW_OUT, length, 1};
    uint8_t sum   = 0;
    for (unsigned i = 0; i < length + 3u; ++i)
        sum += p[i];
    p[length + 3] = checksum_ok ? sum : sum ^ 0x80;
    feed(p, length + 4u);
}
int main(void) {
    assert(gmk_radio_diag_query != NULL && "Optional USB radio diagnostic query is missing");
    md_init();
    md_devs_change(DEVS_BT2, false);
    md_main_task();
    ack();
    uint32_t before[30], after[30];
    snapshot(before, 1);
    assert(before[1] == 0); // No firmware-version reply observed yet.
    raw_packet(31, true);
    assert(raw_delivered == 0); // Observe the rejection; do not change it.
    raw_packet(32, true);
    assert(raw_delivered == 1);
    raw_packet(31, false);
    feed((uint8_t[]){MD_REV_CMD_RAW, MD_REV_CMD_RAW_OUT, 255}, 3);
    feed((uint8_t[]){0xEE}, 1);
    feed((uint8_t[]){MD_REV_CMD_MD_FW_VERSION, 0, MD_REV_CMD_MD_FW_VERSION}, 3);
    uint8_t outgoing[32] = {1};
    md_send_raw(outgoing, sizeof(outgoing));
    snapshot(after, 2);
    assert(after[0] == 1234 && after[1] == 1 && after[2] == 0); // Zero can be a received value.
    assert(after[3] == DEVS_BT2 && after[4] == 255 && after[5] == 2);
    for (unsigned offset = 6; offset <= 18; offset += 12) {
        assert(after[offset] > before[offset]);                // UART bytes prove the radio link was active.
        assert(after[offset + 1] == before[offset + 1] + 1);   // Unknown leading byte.
        assert(after[offset + 4] == before[offset + 4] + 1);   // Bad checksum.
        assert(after[offset + 5] == before[offset + 5] + 4);   // RAW headers, including malformed length.
        assert(after[offset + 6] == before[offset + 6] + 1);   // Oversized length rejected.
        assert(after[offset + 7] == before[offset + 7] + 1);   // Valid 31-byte packet, not delivered.
        assert(after[offset + 8] == before[offset + 8] + 1);   // Valid 32-byte packet.
        assert(after[offset + 10] == before[offset + 10] + 1); // Actual callback dispatch.
        assert(after[offset + 11] == before[offset + 11] + 1); // Outbound RAW request, not RF delivery.
    }
    md_devs_change(DEVS_USB, false);
    snapshot(before, 3);
    assert(memcmp(before + 18, after + 18, 12 * sizeof(uint32_t)) == 0); // USB mode preserves BT history.
    uint8_t p[32];
    request(p, 0, 4);
    assert(gmk_radio_diag_query(p, 32, false, 2000) && p[5] == 1);
    for (unsigned i = 8; i < 32; ++i)
        assert(p[i] == 0);
    request(p, 5, 4);
    assert(gmk_radio_diag_query(p, 32, true, 2000) && p[5] == 2);
    request(p, 0, 4);
    p[3] = 2;
    assert(gmk_radio_diag_query(p, 32, true, 2000) && p[5] == 3);
    request(p, 1, 0xFF);
    assert(gmk_radio_diag_query(p, 32, true, 2000) && p[5] == 4);
    request(p, 0, 4);
    uint8_t unchanged[32];
    memcpy(unchanged, p, sizeof(p));
    assert(!gmk_radio_diag_query(p, 31, true, 2000) && memcmp(p, unchanged, sizeof(p)) == 0);
    p[1] = 'X';
    assert(!gmk_radio_diag_query(p, 32, true, 2000));
    request(p, 0, 5);
    assert(gmk_radio_diag_query(p, 32, true, 2000));
    raw_packet(31, true);
    request(p, 2, 5);
    assert(gmk_radio_diag_query(p, 32, true, 2000) && read_word(p + 12) == before[13]); // Frozen page.
    snapshot(after, 6);
    assert(after[13] == before[13] + 1 && after[25] == before[25]); // USB RAW is not BT RAW.
    puts("PASS: real parser observes 31-byte rejection, 32-byte dispatch, corrupt packets and retained RAM history");
}

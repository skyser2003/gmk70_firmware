/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "receiver.h"
#ifdef CONFIG_GMK70_BATTERY
#    include "../../firmware/zmk/battery/battery.h"
void gmk_battery_get(struct gmk_battery_status *out) {
    *out = (struct gmk_battery_status){.percentage = 50, .valid = true, .cable = true, .age_ms = 10, .queries = 2, .replies = 1};
}
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
unsigned                    receiver_locked;
static uint8_t              flash[0x40000], payload[1024], response[32], request[32];
static unsigned             writes, relays, resets;
static bool                 fail_flash, interleave;
static uint32_t             flash_delay, clock_offset;
static bool                 relay_fail, relay_override;
static uint8_t              relay_reply[32], relayed[32];
static gmk_zmk_update_env_t env;
uint32_t                    k_uptime_get_32(void) {
    return env.uptime_ms + clock_offset;
}
const uint8_t *gmk_flash_view(uint32_t address) {
    assert(address >= GMK_FLASH_BASE && address < GMK_FLASH_BASE + sizeof(flash));
    return flash + address - GMK_FLASH_BASE;
}
uint32_t gmk_running_size(void) {
    return 1024;
}
void gmk_zmk_update_reset(void) {
    assert(!receiver_locked);
    ++resets;
}
bool gmk_wired_update(const uint8_t in[32], uint8_t out[32]) {
    assert(!receiver_locked);
    ++relays;
    memcpy(relayed, in, 32);
    if (relay_fail) return false;
    if (relay_override) {
        memcpy(out, relay_reply, 32);
        return true;
    }
    memcpy(out, in, 32);
    out[5] = GMK_OK;
    return true;
}
bool gmk_flash_page(uint32_t address, const uint8_t data[256]) {
    assert(!receiver_locked);
    ++writes;
    clock_offset += flash_delay;
    if (interleave) {
        uint8_t other[32] = {0x7f, 0x70, 1, 0, GMK_BEGIN, 12}, busy[32];
        other[8]          = 0x99;
        other[16]         = 0;
        other[17]         = 4;
        other[24]         = 1;
        gmk_zmk_receiver_receive(other, busy, true, &env);
        assert(busy[5] == GMK_BUSY && busy[8] == 0x99 && busy[4] == GMK_BEGIN);
        other[4] = GMK_STATUS;
        other[5] = 0;
        memset(other + 16, 0, 16);
        gmk_zmk_receiver_receive(other, busy, true, &env);
        assert(busy[5] == GMK_BUSY);
        uint8_t health_request[32] = {0x7f, 0x72, 1, 0, 0x20, 0, 0, 0, 0x78};
        gmk_zmk_receiver_receive(health_request, busy, true, &env);
        assert(busy[5] == GMK_OK && busy[8] == 0x78 && gmk_read_u32(busy + 12) == 0x4b4d5a47);
    }
    if (fail_flash) return false;
    assert(address == GMK_COMMIT_BASE || (address >= GMK_STAGE_BASE && address < GMK_STAGE_BASE + GMK_APP_SIZE));
    memcpy(flash + address - GMK_FLASH_BASE, data, 256);
    return true;
}
static void setup(void) {
    memset(flash, 0xff, sizeof(flash));
    memset(payload, 0xa5, sizeof(payload));
    gmk_write_u32(payload, 0x20006000);
    gmk_write_u32(payload + 4, GMK_APP_BASE + 9);
    writes = relays = resets = 0;
    fail_flash = interleave = relay_fail = relay_override = false;
    flash_delay = clock_offset = 0;
    env                        = (gmk_zmk_update_env_t){.left = true, .wired = true, .usb_configured = true, .writes_ready = true, .peer = true, .uptime_ms = 100, .matrix_events = 42};
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    env.writes_ready = false;
#endif
    gmk_zmk_receiver_init(&env);
}
static void packet(unsigned op) {
    memset(request, 0, 32);
    request[0] = 0x7f;
    request[1] = 0x70;
    request[2] = 1;
    request[4] = op;
    request[8] = 0x12;
    if (op == GMK_BEGIN) {
        request[5] = 12;
        gmk_write_u32(request + 16, 1024);
        gmk_write_u32(request + 20, gmk_crc32(payload, 1024));
        request[24] = 1;
    }
}
static void receive(void) {
    gmk_zmk_receiver_receive(request, response, true, &env);
}
static void finish(void) {
    gmk_zmk_receiver_step(&env);
    uint8_t status[32] = {0x7f, 0x70, 1, 0, GMK_STATUS};
    gmk_zmk_receiver_receive(status, response, true, &env);
}
static void begin(void) {
    packet(GMK_BEGIN);
    receive();
    assert(response[5] == GMK_BUSY && writes == 0);
    finish();
    assert(response[5] == GMK_OK && response[6] == GMK_RECEIVING && writes == 1);
}
static void gates(void) {
    for (unsigned gate = 0; gate < 4; ++gate) {
        setup();
        packet(GMK_BEGIN);
        if (gate == 0) env.left = false;
        if (gate == 1) env.wired = false;
        if (gate == 2) env.usb_configured = false;
        if (gate == 3) env.writes_ready = false;
        receive();
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
        if (gate == 3) {
            assert(response[5] == GMK_BUSY);
            continue;
        }
#endif
        assert(response[5] == GMK_UNAVAILABLE);
        gmk_zmk_receiver_step(&env);
        assert(!writes);
    }
    setup();
    packet(GMK_BEGIN);
    request[7] = 1;
    receive();
    assert(response[5] == GMK_BAD_REQUEST && !writes);
    packet(GMK_EEPROM_INFO);
    receive();
    assert(response[5] == GMK_UNAVAILABLE);
    setup();
    packet(GMK_BEGIN);
    receive();
    env.wired = false;
    gmk_zmk_receiver_step(&env);
    env.wired = true;
    packet(GMK_STATUS);
    receive();
    assert(response[5] == GMK_UNAVAILABLE && !writes);
}
static void health(void) {
    setup();
    begin();
    uint8_t ack[32];
    memcpy(ack, response, 32);
    memset(request, 0, 32);
    request[0] = 0x7f;
    request[1] = 0x72;
    request[2] = 1;
    request[4] = 0x20;
    request[8] = 0x65;
    receive();
    assert(response[5] == GMK_OK && response[8] == 0x65 && gmk_read_u32(response + 12) == 0x4b4d5a47);
    assert(gmk_read_u32(response + 16) == 100 && gmk_read_u32(response + 20) == 0 && gmk_read_u32(response + 28) == 42);
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    assert(gmk_read_u32(response + 24) == 14);
#else
    assert(gmk_read_u32(response + 24) == 22);
#endif
    packet(GMK_STATUS);
    receive();
    assert(!memcmp(response, ack, 32));
    request[1] = 0x72;
    request[4] = 0x20;
    request[8] = 0;
    receive();
    assert(response[5] == GMK_BAD_REQUEST);
    memset(flash + GMK_APP_BASE - GMK_FLASH_BASE, 0x31, 1024);
    memset(flash + GMK_STAGE_BASE - GMK_FLASH_BASE, 0x72, 1024);
    packet(GMK_INFO);
    receive();
    finish();
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    assert(gmk_read_u32(response + 20) == gmk_crc32(flash + GMK_STAGE_BASE - GMK_FLASH_BASE, 1024));
#else
    assert(gmk_read_u32(response + 20) == gmk_crc32(flash + GMK_APP_BASE - GMK_FLASH_BASE, 1024));
#endif
    assert(response[5] == GMK_OK && response[4] == GMK_INFO && gmk_read_u32(response + 16) == 1024 && gmk_read_u32(response + 24) == 1);
}
static void right_health(void) {
    setup();
    env.left           = false;
    env.usb_configured = false;
    env.writes_ready   = false;
    gmk_zmk_receiver_step(&env);
    uint8_t health_request[32] = {0x7f, 0x72, 1, 1, 0x20, 0, 0, 0, 0x43};
    gmk_zmk_receiver_receive(health_request, response, false, &env);
    assert(response[5] == GMK_OK && response[3] == 1 && response[7] == 0 && response[8] == 0x43 && !writes && !relays);
    assert(!(gmk_read_u32(response + 24) & 1));
}

static void failures_timeout(void) {
    setup();
    fail_flash = true;
    packet(GMK_BEGIN);
    receive();
    finish();
    assert(response[5] == GMK_FLASH_ERROR && response[8] == 0x12 && response[6] == GMK_FAILED);
    setup();
    interleave = true;
    begin();
    interleave = false;
    env.uptime_ms += 10001;
    gmk_zmk_receiver_step(&env);
    packet(GMK_BEGIN);
    receive();
    finish();
    assert(response[5] == GMK_OK && writes == 2);
}
static void remote(void) {
    setup();
    packet(GMK_INFO);
    request[3] = 1;
    receive();
    assert(response[5] == GMK_BUSY && !relays);
    gmk_zmk_receiver_step(&env);
    assert(relays == 1);
    setup();
    packet(GMK_BEGIN);
    request[3] = 1;
    receive();
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    assert(response[5] == GMK_UNAVAILABLE);
    gmk_zmk_receiver_step(&env);
    assert(!relays);
#else
    assert(response[5] == GMK_BUSY);
    gmk_zmk_receiver_step(&env);
    assert(relays == 1 && !writes);
    setup();
    env.left           = false;
    env.usb_configured = false;
    packet(GMK_BEGIN);
    request[3] = 1;
    gmk_zmk_receiver_receive(request, response, false, &env);
    assert(response[5] == GMK_BUSY);
    gmk_zmk_receiver_step(&env);
    assert(writes == 1);
    setup();
    env.left           = false;
    env.usb_configured = false;
    env.writes_ready   = false;
    packet(GMK_BEGIN);
    request[3] = 1;
    gmk_zmk_receiver_receive(request, response, false, &env);
    assert(response[5] == GMK_UNAVAILABLE && !writes);
#endif
}
static void timeout_with_read_traffic(void) {
    setup();
    begin();
    env.uptime_ms += 10001;
    packet(GMK_INFO);
    receive();
    finish();
    assert(response[5] == GMK_OK && response[6] == GMK_IDLE);
    packet(GMK_BEGIN);
    receive();
    finish();
    assert(response[5] == GMK_OK && writes == 2);
}

static void relay_progress(void) {
    setup();
    packet(GMK_INFO);
    request[3]     = 1;
    relay_override = true;
    memcpy(relay_reply, request, 32);
    relay_reply[5] = GMK_BUSY;
    receive();
    gmk_zmk_receiver_step(&env);
    assert(relays == 1);
    packet(GMK_STATUS);
    request[3] = 1;
    receive();
    assert(response[5] == GMK_BUSY && response[4] == GMK_INFO);
    relay_reply[5]  = GMK_OK;
    relay_reply[20] = 0x33;
    gmk_zmk_receiver_step(&env);
    receive();
    assert(response[5] == GMK_OK && response[20] == 0x33 && relays == 2);
    packet(GMK_INFO);
    request[3] = 1;
    receive();
    assert(response[5] == GMK_BUSY);
    relay_reply[20] = 0x44;
    gmk_zmk_receiver_step(&env);
    assert(relays == 3);
    packet(GMK_STATUS);
    request[3] = 1;
    receive();
    assert(response[20] == 0x44);
    uint8_t normal_ack[32];
    memcpy(normal_ack, response, 32);
    memset(request, 0, 32);
    request[0] = 0x7f;
    request[1] = 0x72;
    request[2] = 1;
    request[3] = 1;
    request[4] = 0x20;
    request[8] = 0x77;
    memcpy(relay_reply, request, 32);
    relay_reply[5] = GMK_OK;
    gmk_write_u32(relay_reply + 12, 0x4b4d5a47);
    receive();
    assert(response[5] == GMK_BUSY);
    gmk_zmk_receiver_step(&env);
    receive();
    assert(response[5] == GMK_OK && response[8] == 0x77 && gmk_read_u32(response + 12) == 0x4b4d5a47);
    packet(GMK_STATUS);
    request[3] = 1;
    receive();
    assert(!memcmp(response, normal_ack, 32));
    packet(GMK_INFO);
    request[3] = 1;
    request[8]++;
    relay_fail = true;
    receive();
    gmk_zmk_receiver_step(&env);
    packet(GMK_STATUS);
    request[3] = 1;
    receive();
    assert(response[5] == GMK_UNAVAILABLE && response[8] == 0x13);
}

#ifndef CONFIG_GMK70_STAGING_QUALIFICATION
static void bad_payload(void) {
    setup();
    begin();
    for (unsigned offset = 0; offset < 256; offset += 16) {
        packet(GMK_DATA);
        request[5] = 16;
        gmk_write_u32(request + 12, offset);
        memcpy(request + 16, payload + offset, 16);
        if (offset == 240) fail_flash = true;
        receive();
        finish();
    }
    assert(response[5] == GMK_FLASH_ERROR && response[6] == GMK_FAILED && gmk_read_u32(response + 12) == 240);
    setup();
    begin();
    for (unsigned offset = 0; offset < 1024; offset += 16) {
        packet(GMK_DATA);
        request[5] = 16;
        gmk_write_u32(request + 12, offset);
        memcpy(request + 16, payload + offset, 16);
        receive();
        finish();
    }
    flash[GMK_STAGE_BASE - GMK_FLASH_BASE + 500] ^= 1;
    packet(GMK_SEAL);
    receive();
    finish();
    assert(response[5] == GMK_BAD_IMAGE && response[6] == GMK_FAILED);
    for (unsigned i = 0; i < 256; ++i)
        assert(flash[GMK_COMMIT_BASE - GMK_FLASH_BASE + i] == 0xff);
}
#endif

static void transfer(void) {
    setup();
    begin();
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
    for (unsigned op = GMK_DATA; op <= GMK_INSTALL; ++op) {
        packet(op);
        if (op == GMK_DATA) {
            request[5] = 16;
            memcpy(request + 16, payload, 16);
        }
        receive();
        assert(response[5] == GMK_UNAVAILABLE);
        gmk_zmk_receiver_step(&env);
        assert(writes == 1);
    }
    packet(GMK_ABORT);
    receive();
    finish();
    assert(response[5] == GMK_OK && response[6] == GMK_IDLE && !resets);
#else
    for (unsigned offset = 0; offset < 1024; offset += 16) {
        packet(GMK_DATA);
        request[5] = 16;
        gmk_write_u32(request + 12, offset);
        memcpy(request + 16, payload + offset, 16);
        receive();
        finish();
        assert(response[5] == GMK_OK && gmk_read_u32(response + 12) == offset + 16);
        unsigned before = writes;
        receive();
        finish();
        assert(response[5] == GMK_OK && writes == before);
        request[16] ^= 1;
        receive();
        finish();
        assert(response[5] == GMK_BAD_OFFSET && gmk_read_u32(response + 12) == offset);
        request[16] ^= 1;
    }
    packet(GMK_SEAL);
    receive();
    finish();
    assert(response[5] == GMK_OK && response[6] == GMK_SEALED);
    packet(GMK_INSTALL);
    flash_delay = 400;
    receive();
    finish();
    assert(response[5] == GMK_OK && response[6] == GMK_COMMITTED && !resets);
    flash_delay = 0;
    env.uptime_ms += clock_offset;
    clock_offset = 0;
    gmk_image_t image;
    assert(gmk_manifest_decode(gmk_flash_view(GMK_COMMIT_BASE), &image));
    env.uptime_ms += 299;
    gmk_zmk_receiver_step(&env);
    assert(!resets);
    receive();
    gmk_zmk_receiver_step(&env);
    assert(!resets);
    env.uptime_ms++;
    gmk_zmk_receiver_step(&env);
    assert(resets == 1);
#endif
}
#ifdef CONFIG_GMK70_ZMK_PEER_TRIAL_RELAY
static void trial_relay(void) {
    setup();
    uint8_t normal_ack[32];
    packet(GMK_INFO);
    request[3] = 1;
    receive();
    gmk_zmk_receiver_step(&env);
    packet(GMK_STATUS);
    request[3] = 1;
    receive();
    memcpy(normal_ack, response, 32);
    packet(GMK_BEGIN);
    request[1] = 0x71;
    request[2] = 2;
    request[3] = 1;
    request[5] = 16;
    gmk_write_u32(request + 24, GMK_STAGE_BASE);
    gmk_write_u32(request + 28, 0x20006fc0);
    uint8_t original[32];
    memcpy(original, request, 32);
    receive();
    assert(response[5] == GMK_BUSY && response[1] == 0x71 && response[2] == 2 && !writes);
    gmk_zmk_receiver_step(&env);
    assert(!memcmp(relayed, original, 32) && !writes);
    packet(GMK_STATUS);
    request[1] = 0x71;
    request[2] = 2;
    request[3] = 1;
    receive();
    assert(response[5] == GMK_OK && response[4] == GMK_BEGIN && response[1] == 0x71);
    packet(GMK_STATUS);
    request[3] = 1;
    receive();
    assert(!memcmp(response, normal_ack, 32));
    memcpy(request, original, 32);
    request[3] = 0;
    receive();
    assert(response[5] == GMK_UNAVAILABLE && !writes);
    memcpy(request, original, 32);
    env.left = false;
    gmk_zmk_receiver_receive(request, response, false, &env);
    assert(response[5] == GMK_UNAVAILABLE && !writes);
    env.left = true;
    for (unsigned gate = 0; gate < 4; ++gate) {
        setup();
        memcpy(request, original, 32);
        if (gate == 0) env.left = false;
        if (gate == 1) env.usb_configured = false;
        if (gate == 2) env.wired = false;
        if (gate == 3) env.writes_ready = false;
        receive();
        assert(response[5] == GMK_UNAVAILABLE && !relays && !writes);
    }
    setup();
    env.writes_ready = false;
    packet(GMK_INFO);
    request[1] = 0x71;
    request[2] = 2;
    request[3] = 1;
    request[4] = 0x13;
    gmk_write_u32(request + 8, 0x87654321);
    gmk_write_u32(request + 12, 0x10203040);
    receive();
    assert(response[5] == GMK_BUSY && gmk_read_u32(response + 8) == 0x87654321 && gmk_read_u32(response + 12) == 0x10203040);
    uint8_t caps[32];
    memcpy(caps, request, 32);
    gmk_zmk_receiver_step(&env);
    assert(!memcmp(relayed, caps, 32));
    packet(GMK_STATUS);
    request[1] = 0x71;
    request[2] = 2;
    request[3] = 1;
    receive();
    assert(response[5] == GMK_OK && response[4] == 0x13 && gmk_read_u32(response + 12) == 0x10203040);
    setup();
    memcpy(request, original, 32);
    request[5] = 12;
    receive();
    assert(response[5] == GMK_BAD_REQUEST && !relays);
    setup();
    memcpy(request, original, 32);
    receive();
    env.writes_ready = false;
    gmk_zmk_receiver_step(&env);
    assert(!relays && !writes);
    env.writes_ready = true;
    packet(GMK_STATUS);
    request[1] = 0x71;
    request[2] = 2;
    request[3] = 1;
    receive();
    assert(response[5] == GMK_UNAVAILABLE && response[8] == 0x12);
}
#endif
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
static void studio_size_bounds(void) {
    setup();
    packet(GMK_BEGIN);
    gmk_write_u32(request + 16, 0x1a000);
    receive();
    finish();
    assert(response[5] == GMK_OK && writes == 1);
    setup();
    packet(GMK_BEGIN);
    gmk_write_u32(request + 16, 0x1a004);
    receive();
    finish();
    assert(response[5] == GMK_BAD_REQUEST && writes == 0);
    setup();
    packet(GMK_BEGIN);
    request[3] = 1;
    gmk_write_u32(request + 16, 0x1e000);
    receive();
    gmk_zmk_receiver_step(&env);
    assert(relays == 1 && writes == 0);
}
static void storage_exclusion(void) {
    setup();
    assert(gmk_zmk_receiver_storage_claim());
    assert(!gmk_zmk_receiver_storage_claim());
    packet(GMK_BEGIN);
    receive();
    assert(response[5] == GMK_UNAVAILABLE && writes == 0);
    gmk_zmk_receiver_step(&env);
    assert(writes == 0);
    gmk_zmk_receiver_storage_release();
    packet(GMK_INFO);
    receive();
    assert(!gmk_zmk_receiver_storage_claim());
    gmk_zmk_receiver_step(&env);
    assert(gmk_zmk_receiver_storage_claim());
    gmk_zmk_receiver_storage_release();
    begin();
    assert(!gmk_zmk_receiver_storage_claim());
}
#endif
int main(void) {
#ifdef CONFIG_GMK70_BATTERY
    setup();
    uint8_t battery_request[32] = {0x7f, 0x72, 1, 0, 0x21, 0, 0, 0, 0x79};
    env.writes_ready            = false; /* Read-only even when mutation admission is closed. */
    gmk_zmk_receiver_receive(battery_request, response, true, &env);
    assert(response[5] == GMK_OK && gmk_read_u32(response + 12) == 0x54544142 && response[16] == 50);
    env.usb_configured = false;
    gmk_zmk_receiver_receive(battery_request, response, true, &env);
    assert(response[5] == GMK_UNAVAILABLE);
    env.usb_configured = true;
    env.wired          = false;
    gmk_zmk_receiver_receive(battery_request, response, true, &env);
    assert(response[5] == GMK_UNAVAILABLE);
    env.wired = true;
    gmk_zmk_receiver_receive(battery_request, response, false, &env);
    assert(response[5] == GMK_UNAVAILABLE && writes == 0 && relays == 0);
    battery_request[3] = 1;
    gmk_zmk_receiver_receive(battery_request, response, true, &env);
    assert(response[5] == GMK_BAD_REQUEST && relays == 0);
#endif
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    storage_exclusion();
    studio_size_bounds();
#endif
    setup();
    begin();
    gates();
    health();
    right_health();
    failures_timeout();
    remote();
#ifndef CONFIG_GMK70_ZMK_PEER_TRIAL_RELAY
    setup();
    packet(GMK_INFO);
    request[1] = 0x71;
    request[2] = 2;
    request[3] = 1;
    receive();
    assert(response[5] == GMK_BAD_REQUEST && !relays);
#endif
    timeout_with_read_traffic();
    relay_progress();
    transfer();
#ifndef CONFIG_GMK70_STAGING_QUALIFICATION
    bad_payload();
#endif
#ifdef CONFIG_GMK70_ZMK_PEER_TRIAL_RELAY
    trial_relay();
#endif
    puts("ZMK receiver tests passed");
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reuse the real flash/clock/transport fixture, not an alternate receiver. */
#define main baseline_receiver_tests_main
#include "receiver_test.c"
#undef main

static void control_setup(void) {
    setup();
    env.watchdog_ready = false;
    env.feed_calls     = 0;
    gmk_zmk_receiver_init(&env);
}

int main(void) {
    control_setup();
    begin(); /* The explicit diagnostic permits local writes without IWDG. */
    for (unsigned offset = 0; offset < sizeof(payload); offset += 16) {
        packet(GMK_DATA);
        request[5] = 16;
        gmk_write_u32(request + 12, offset);
        memcpy(request + 16, payload + offset, 16);
        receive();
        finish();
        assert(response[5] == GMK_OK);
    }
    packet(GMK_SEAL);
    receive();
    finish();
    assert(response[5] == GMK_OK && response[6] == GMK_SEALED);
    packet(GMK_INSTALL);
    receive();
    finish();
    assert(response[5] == GMK_OK && response[6] == GMK_COMMITTED);
    env.uptime_ms += 300;
    gmk_zmk_receiver_step(&env);
    assert(resets == 1);

    for (unsigned gate = 0; gate < 5; ++gate) {
        control_setup();
        packet(GMK_BEGIN);
        bool usb = true;
        if (gate == 0) env.left = false;
        if (gate == 1) env.wired = false;
        if (gate == 2) env.usb_configured = false;
        if (gate == 3) request[3] = 1;
        if (gate == 4) {
            env.left   = false;
            request[3] = 1;
            usb        = false;
        }
        gmk_zmk_receiver_receive(request, response, usb, &env);
        assert(response[5] == GMK_UNAVAILABLE);
        gmk_zmk_receiver_step(&env);
        assert(writes == 0 && relays == 0);
    }
    control_setup();
    packet(GMK_BEGIN);
    receive();
    env.wired = false;
    gmk_zmk_receiver_step(&env);
    assert(!writes);

    control_setup();
    uint8_t query[32] = {0x7f, 0x72, 1, 0, 0x20, 0, 0, 0, 1};
    gmk_zmk_receiver_receive(query, response, true, &env);
    assert(response[5] == GMK_OK && gmk_read_u32(response + 20) == 0);
    assert(gmk_read_u32(response + 24) == (16U | 2U | 4U));
    /* Read-only peer identities remain available; no relay programming. */
    packet(GMK_INFO);
    request[3] = 1;
    receive();
    gmk_zmk_receiver_step(&env);
    assert(relays == 1 && writes == 0);
    puts("ZMK watchdog rollback local-only receiver tests passed");
}

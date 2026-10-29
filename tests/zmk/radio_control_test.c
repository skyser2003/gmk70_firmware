/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "control.h"
#include "radio.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static struct gmk_radio_status cached = {.errors = 0x04030201, .selections = 0x08070605, .reports = 0x0c0b0a09, .selected = 2, .active = 1, .connection = 2, .queued = 8, .waiting = true, .available = true};
static unsigned                reads, selections, markers;
static int                     selection_result, marker_result;
static uint8_t                 selected_channel;
static bool                    selected_pair;
void                           gmk_radio_get(struct gmk_radio_status *out) {
    ++reads;
    *out = cached;
}
int gmk_radio_select(uint8_t channel, bool pair) {
    ++selections;
    selected_channel = channel;
    selected_pair    = pair;
    return selection_result;
}
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
int gmk_radio_test_marker(void) {
    ++markers;
    return marker_result;
}
#endif
static const uint8_t        request[32] = {0x7f, 0x72, 1, 0, 0x22, 0, 0, 0, 0x12, 0x34, 0x56, 0x78};
static gmk_zmk_update_env_t env         = {.left = true, .wired = true, .usb_configured = true, .writes_ready = true};

static void strict_packet_shape(void) {
    assert(gmk_radio_control_packet(request));
    uint8_t changed[32];
    for (unsigned offset = 0; offset < 32; ++offset) {
        if ((offset >= 5 && offset <= 6) || (offset >= 8 && offset <= 11)) continue;
        memcpy(changed, request, sizeof(changed));
        changed[offset] ^= 1;
        assert(!gmk_radio_control_packet(changed));
    }
    for (unsigned action = 0; action <= 4; ++action)
        for (unsigned channel = 0; channel <= 7; ++channel) {
            memcpy(changed, request, sizeof(changed));
            changed[5] = (uint8_t)action;
            changed[6] = (uint8_t)channel;
            bool valid = action == 0 ? channel == 0 : action == 1 ? channel <= 6 : action == 2 ? channel >= 1 && channel <= 6 : action == 3 && channel == 0;
            assert(gmk_radio_control_packet(changed) == valid);
        }
    memcpy(changed, request, sizeof(changed));
    memset(changed + 8, 0, 4);
    assert(!gmk_radio_control_packet(changed));
    memset(changed + 8, 0xff, 4);
    assert(gmk_radio_control_packet(changed));
}
static void cached_reply_and_admission(void) {
    struct {
        uint8_t before, bytes[32], after;
    } out            = {.before = 0x5a, .after = 0xa5};
    env.writes_ready = false;
    gmk_radio_control_receive(request, out.bytes, true, &env);
    const uint8_t expected[32] = {0x7f, 0x72, 1,    0,    0x22, 0,
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
                                  7,
#else
                                  3,
#endif
                                  3,    0x12, 0x34, 0x56, 0x78, 'R', 'A', 'D', 'I', 2, 1, 2, 8, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    assert(!memcmp(out.bytes, expected, sizeof(expected)));
    assert(out.before == 0x5a && out.after == 0xa5);
    assert(reads == 1 && !selections && !markers);
    env.writes_ready = true;
    for (unsigned gate = 0; gate < 4; ++gate) {
        gmk_zmk_update_env_t denied = env;
        if (gate == 1) denied.left = false;
        if (gate == 2) denied.wired = false;
        if (gate == 3) denied.usb_configured = false;
        unsigned before = reads;
        gmk_radio_control_receive(request, out.bytes, gate != 0, &denied);
        if (gate == 2)
            assert(out.bytes[5] == 0 && reads == before + 1 && !selections && !markers);
        else
            assert(out.bytes[5] == 2 && reads == before && !selections && !markers);
        assert(out.bytes[7] == (denied.left ? 3 : 0));
    }
    uint8_t invalid[32];
    memcpy(invalid, request, 32);
    invalid[31] = 1;
    memset(out.bytes, 0xa5, 32);
    unsigned before = reads;
    gmk_radio_control_receive(invalid, out.bytes, true, &env);
    for (unsigned i = 0; i < 32; ++i)
        assert(out.bytes[i] == 0);
    assert(reads == before && !selections && !markers);
}
static void actions_only_queue_and_never_retry(void) {
    uint8_t in[32], out[32];
    for (uint8_t action = 1; action <= 2; ++action)
        for (uint8_t channel = action == 2 ? 1 : 0; channel <= 6; ++channel) {
            memcpy(in, request, 32);
            in[5]           = action;
            in[6]           = channel;
            unsigned before = selections;
            gmk_radio_control_receive(in, out, true, &env);
            assert(out[5] == 0 && selections == before + 1);
            assert(selected_channel == channel && selected_pair == (action == 2));
            assert(out[16] == cached.selected && out[17] == cached.active);
        }
    env.writes_ready      = false;
    unsigned before       = selections;
    unsigned cached_reads = reads;
    gmk_radio_control_receive(in, out, true, &env);
    assert(out[5] == 2 && selections == before && reads == cached_reads);
    env.writes_ready = true;
    selection_result = -EBUSY;
    gmk_radio_control_receive(in, out, true, &env);
    assert(out[5] == 1 && selections == before + 1);
    selection_result = -ENODEV;
    gmk_radio_control_receive(in, out, true, &env);
    assert(out[5] == 2 && selections == before + 2);
    selection_result = 0;
    memcpy(in, request, 32);
    in[5] = 3;
    gmk_radio_control_receive(in, out, true, &env);
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
    assert(out[5] == 0 && markers == 1);
    marker_result = -EBUSY;
    gmk_radio_control_receive(in, out, true, &env);
    assert(out[5] == 1 && markers == 2);
#else
    assert(out[5] == 2 && markers == 0);
    (void)marker_result;
#endif
}
int main(void) {
    strict_packet_shape();
    cached_reply_and_admission();
    actions_only_queue_and_never_retry();
    puts("Radio control: strict shape, cached status, USB/role/readiness gates, bounded queue admission and qualification marker passed");
    return 0;
}

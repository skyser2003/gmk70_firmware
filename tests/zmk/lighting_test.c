/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_GMK70_WB32_RGB 1
#include "../../firmware/zmk/lighting/lighting.c"
int64_t         fake_ms;
static bool     left = true, wired = true, inject;
static unsigned writes, transmitted, initialization;
static uint8_t  received[123], packet[16];
static size_t   pixel_count;
bool            gmk_wired_is_left(void) {
    return left;
}
bool gmk_wired_mode(void) {
    return wired;
}
int gmk_pixels_init(void) {
    ++initialization;
    return 0;
}
int gmk_pixels_write(const uint8_t *rgb, size_t count) {
    assert(!frame_lock.held);
    ++writes;
    pixel_count = count;
    memcpy(received, rgb, 3 * count);
    if (inject) {
        uint8_t before[123];
        memcpy(before, rgb, 123);
        gmk_lighting_receive(packet);
        assert(!memcmp(before, rgb, 123));
    }
    return 0;
}
bool __wrap_gmk_protocol_lighting(const struct gmk_wire_io *io, const uint8_t in[16]) {
    (void)io;
    assert(!frame_lock.held);
    ++transmitted;
    memcpy(packet, in, 16);
    return true;
}
int main(void) {
    assert(!init(NULL) && initialization == 1 && length(NULL) == 77);
    struct led_rgb colors[77];
    for (unsigned i = 0; i < 77; ++i)
        colors[i] = (struct led_rgb){(uint8_t)i, 7, 29};
    assert(update_rgb(NULL, colors, 76) == -EINVAL && writes == 0);
    assert(!update_rgb(NULL, colors, 77));
    assert(writes == 1 && pixel_count == 36);
    for (unsigned i = 0; i < 36; ++i)
        assert(received[3 * i] == i);
#ifdef CONFIG_GMK70_RGB_LOCAL_ONLY
    for (unsigned i = 0; i < 40; ++i) {
        fake_ms += 250;
        gmk_lighting_wire_poll(NULL);
        assert(transmitted == 0 && !frame.pending);
    }
    puts("Local-only RGB never sends frames to an unchanged older peer");
    return 0;
#endif
    struct gmk_light_frame rx = {0};
    for (unsigned i = 0; i < 11; ++i) {
        gmk_lighting_wire_poll(NULL);
        assert(transmitted == i + 1);
        assert(gmk_light_accept(&rx, packet));
    }
    assert(gmk_light_claim(&rx));
    for (unsigned i = 0; i < 41; ++i)
        assert(rx.rgb[3 * i] == i + 36);
    gmk_lighting_wire_poll(NULL);
    assert(transmitted == 11);
    fake_ms = 199;
    gmk_lighting_wire_poll(NULL);
    assert(transmitted == 11);
    fake_ms = 200;
    gmk_lighting_wire_poll(NULL);
    assert(transmitted == 12 && packet[2] == 0); /* Repaint without another animation tick. */
    gmk_lighting_poll();
    assert(writes == 1); /* Central does not render the remote cache. */
    left = false;
    assert(!update_rgb(NULL, colors, 77) && writes == 1);
    memset(&frame, 0, sizeof(frame));
    gmk_light_release(&rx);
    struct gmk_light_frame tx = {0};
    assert(gmk_light_queue(&tx, rx.rgb));
    for (unsigned i = 0; i < 11; ++i) {
        assert(gmk_light_next(&tx, packet));
        gmk_lighting_receive(packet);
        gmk_light_sent(&tx, packet, true);
        if (i < 10) {
            gmk_lighting_poll();
            assert(writes == 1);
        }
    }
    inject = true;
    gmk_lighting_poll();
    assert(writes == 2 && pixel_count == 41);
    for (unsigned i = 0; i < 41; ++i)
        assert(received[3 * i] == i + 36);
    gmk_lighting_poll();
    assert(writes == 2);
    inject = false;
    wired  = false;
    assert(!update_rgb(NULL, colors, 77));
    assert(writes == 3 && pixel_count == 41 && received[0] == 36);
    gmk_lighting_poll();
    assert(!frame.pending && !frame.ready && !frame.receiving);
    puts("Lighting adapter: half slicing, one chunk per scan, worker-only DMA and claimed-buffer ownership passed");
}

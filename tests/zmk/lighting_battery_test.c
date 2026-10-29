/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_GMK70_WB32_RGB 1
#define CONFIG_GMK70_BATTERY 1
#include "../../firmware/zmk/lighting/lighting.c"
#include "battery.h"

int64_t                          fake_ms;
static struct gmk_battery_status status = {.valid = true, .percentage = 50, .cable = true};
static unsigned                  writes;
static uint8_t                   last_pixels[123];
static uint8_t                   physical_mode = 3;
static struct gmk_light_frame    peer_frame;
bool                             gmk_wired_is_left(void) {
    return true;
}
bool gmk_wired_mode(void) {
    return physical_mode == 3;
}
bool gmk_split_available(void) {
#ifdef CONFIG_GMK70_RADIO
    return physical_mode != 0;
#else
    return gmk_wired_mode();
#endif
}
void gmk_battery_get(struct gmk_battery_status *out) {
    *out = status;
}
int gmk_pixels_init(void) {
    return 0;
}
int gmk_pixels_write(const uint8_t *rgb, size_t count) {
    assert(count == 36);
    ++writes;
    memcpy(last_pixels, rgb, 3 * count);
    return 0;
}
bool __wrap_gmk_protocol_lighting(const struct gmk_wire_io *io, const uint8_t packet[16]) {
    (void)io;
    assert(gmk_light_accept(&peer_frame, packet));
    return true;
}
int main(void) {
    assert(!init(NULL));
    struct led_rgb colors[77];
    memset(colors, 7, sizeof(colors));
    assert(!update_rgb(NULL, colors, 77) && writes == 1);
    assert(!gmk_lighting_battery_set(GMK_BATTERY_LEVEL, 55, true));
    gmk_lighting_poll();
    assert(writes == 2 && last_pixels[15] == 0 && last_pixels[16] == 80);
    for (unsigned i = 0; i < sizeof(colors); ++i)
        assert(((uint8_t *)colors)[i] == 7);
    memset(colors, 0, sizeof(colors)); /* Native RGB off, while the query stays held. */
    assert(!update_rgb(NULL, colors, 77) && writes == 3 && last_pixels[16] == 80);
    assert(!gmk_lighting_battery_set(GMK_BATTERY_LEVEL, 55, false));
    gmk_lighting_poll();
    assert(writes == 4);
    for (unsigned i = 0; i < 108; ++i)
        assert(!last_pixels[i]);
    for (unsigned i = 0; i < 123; ++i)
        assert(!frame.rgb[i]);
    assert(!gmk_lighting_battery_set(GMK_BATTERY_CHARGING, 3, true));
    gmk_lighting_poll();
    assert(writes == 5 && last_pixels[99] == 80 && last_pixels[100] == 0);
    gmk_lighting_poll();
    assert(writes == 5); /* No DMA busy loop for an unchanged held status. */
    status.full = true;
    gmk_lighting_poll();
    assert(writes == 6 && last_pixels[99] == 0 && last_pixels[100] == 80);
    assert(!gmk_lighting_battery_set(GMK_BATTERY_CHARGING, 3, false));
    gmk_lighting_poll();
    assert(writes == 7 && !last_pixels[99] && !last_pixels[100]);
    status.percentage = 100;
    assert(!gmk_lighting_battery_set(GMK_BATTERY_LEVEL, 55, true));
    gmk_lighting_poll();
    for (unsigned i = 0; i < GMK_LIGHT_CHUNKS; ++i)
        gmk_lighting_wire_poll(NULL);
    assert(peer_frame.rgb[10] == 80); /* Right number-row starts at global39. */
    physical_mode = 0;
    gmk_lighting_poll();
    physical_mode = 3;
    gmk_lighting_poll();
    for (unsigned i = 0; i < GMK_LIGHT_CHUNKS; ++i)
        gmk_lighting_wire_poll(NULL);
    assert(peer_frame.rgb[10] == 0); /* Rejoining clears a stale peer overlay while RGB is off. */
    assert(!gmk_lighting_battery_set(GMK_BATTERY_LEVEL, 12, true));
    gmk_lighting_poll();
    assert(!gmk_lighting_battery_set(GMK_BATTERY_CHARGING, 12, false));
    assert(!gmk_lighting_battery_release(12)); /* Release after Studio replaces the held binding. */
    gmk_lighting_poll();
    for (unsigned i = 0; i < 108; ++i)
        assert(!last_pixels[i]);
    unsigned unchanged = writes;
    assert(!gmk_lighting_battery_release(4)); /* Ordinary releases do not redraw. */
    gmk_lighting_poll();
    assert(writes == unchanged);
#ifdef CONFIG_GMK70_RADIO
    for (uint8_t mode = 1; mode <= 2; ++mode) {
        physical_mode = mode;
        assert(!gmk_wired_mode() && gmk_split_available());
        memset(colors, 7, sizeof(colors));
        assert(!update_rgb(NULL, colors, 77));
        assert(!gmk_lighting_battery_set(GMK_BATTERY_LEVEL, 55, true));
        gmk_lighting_poll();
        assert(last_pixels[16] == 80); /* Wireless mode retains the held overlay. */
        for (unsigned i = 0; i < GMK_LIGHT_CHUNKS; ++i)
            gmk_lighting_wire_poll(NULL);
        assert(peer_frame.rgb[10] == 80);
        assert(!gmk_lighting_battery_release(55));
        gmk_lighting_poll();
        for (unsigned i = 0; i < GMK_LIGHT_CHUNKS; ++i)
            gmk_lighting_wire_poll(NULL);
        assert(last_pixels[16] == 7 && peer_frame.rgb[10] == 7);
    }
    assert(!gmk_lighting_battery_set(GMK_BATTERY_LEVEL, 55, true));
    gmk_lighting_poll();
    physical_mode = 0;
    gmk_lighting_poll();
    assert(!gmk_battery_view_active(&battery_view));
    assert(!frame.pending && !frame.ready && !frame.receiving);
#endif
    puts("Held battery lighting: off-state visibility, current-base restore, status refresh and no caller mutation passed");
}

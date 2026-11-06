/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_GMK70_WB32_RGB 1
#define CONFIG_GMK70_BATTERY 1
#ifdef CONFIG_GMK70_CONTROLS
#    include <zmk/activity.h>
static bool                    controls_visible;
static uint32_t                controls_generation;
static enum zmk_activity_state controls_activity = ZMK_ACTIVITY_ACTIVE;
enum zmk_activity_state        zmk_activity_get_state(void) {
    return controls_activity;
}
uint32_t gmk_controls_visual_token(uint32_t now) {
    (void)now;
    return controls_generation;
}
void gmk_controls_overlay(uint8_t *rgb, size_t count, uint32_t now, bool left, bool enabled) {
    (void)now;
    assert(left && count == 77);
    if (controls_visible && enabled) rgb[3 * 18] = 80;
}
#endif
#include "../../firmware/zmk/lighting/lighting.c"
#include "battery.h"

int64_t                          fake_ms;
static struct gmk_battery_status status = {.valid = true, .percentage = 50, .cable = true};
static unsigned                  writes;
static unsigned                  battery_requests;
static int                       write_result;
static bool                      test_left = true;
static uint8_t                   last_pixels[123];
static uint8_t                   physical_mode = 3;
static struct gmk_light_frame    peer_frame;
bool                             gmk_wired_is_left(void) {
    return test_left;
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
void gmk_battery_request(void) {
    ++battery_requests;
}
int gmk_pixels_init(void) {
    return 0;
}
int gmk_pixels_write(const uint8_t *rgb, size_t count) {
    assert(count == (test_left ? 36U : 41U));
    ++writes;
    memcpy(last_pixels, rgb, 3 * count);
    return write_result;
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
    assert(battery_requests == 1);
    gmk_lighting_poll();
    assert(writes == 2 && last_pixels[15] == 0 && last_pixels[16] == 80);
    for (unsigned i = 0; i < sizeof(colors); ++i)
        assert(((uint8_t *)colors)[i] == 7);
    memset(colors, 0, sizeof(colors)); /* Native RGB off, while the query stays held. */
    assert(!update_rgb(NULL, colors, 77) && writes == 3 && last_pixels[16] == 80);
    assert(!gmk_lighting_battery_set(GMK_BATTERY_LEVEL, 55, false));
    assert(battery_requests == 1);
    gmk_lighting_poll();
    assert(writes == 4);
    for (unsigned i = 0; i < 108; ++i)
        assert(!last_pixels[i]);
    for (unsigned i = 0; i < 123; ++i)
        assert(!frame.rgb[i]);
    assert(!gmk_lighting_battery_set(GMK_BATTERY_CHARGING, 3, true));
    assert(battery_requests == 1);
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
#ifdef CONFIG_GMK70_CONTROLS
    physical_mode = 3;
    memset(colors, 0, sizeof(colors));
    assert(!update_rgb(NULL, colors, 77));
    controls_visible = true;
    ++controls_generation;
    gmk_lighting_poll();
    assert(last_pixels[3 * 18] == 80); /* Native host state can light an otherwise off base. */
    assert(!gmk_lighting_battery_set(GMK_BATTERY_LEVEL, 55, true));
    gmk_lighting_poll();
    assert(last_pixels[3 * 18] == 0 && last_pixels[16] == 80); /* Battery hold wins. */
    assert(!gmk_lighting_battery_release(55));
    gmk_lighting_poll();
    assert(last_pixels[3 * 18] == 80);
    controls_activity = ZMK_ACTIVITY_IDLE;
    gmk_lighting_poll();
    assert(last_pixels[3 * 18] == 0); /* Native idle suppresses status power. */
    controls_activity = ZMK_ACTIVITY_ACTIVE;
    gmk_lighting_poll();
    assert(last_pixels[3 * 18] == 80);
    controls_visible = false;
    ++controls_generation;
    gmk_lighting_poll();
    assert(last_pixels[3 * 18] == 0);
    status  = (struct gmk_battery_status){.valid = true, .percentage = 15};
    fake_ms = 0;
    gmk_lighting_poll();
    assert(last_pixels[3 * 33] == 80 && !last_pixels[3 * 33 + 1]);
    fake_ms = 250;
    gmk_lighting_poll();
    assert(!last_pixels[3 * 33]);
    fake_ms = 500;
    gmk_lighting_poll();
    assert(last_pixels[3 * 33] == 80);
    status.age_ms = 10001;
    gmk_lighting_poll();
    assert(!last_pixels[3 * 33]); /* An expired sample removes the warning. */
    status.age_ms     = 0;
    controls_activity = ZMK_ACTIVITY_IDLE;
    gmk_lighting_poll();
    assert(!last_pixels[3 * 33]);
    controls_activity = ZMK_ACTIVITY_ACTIVE;
    gmk_lighting_poll();
    assert(last_pixels[3 * 33] == 80);
    status.cable = true;
    gmk_lighting_poll();
    assert(!last_pixels[3 * 33]);
#endif
#ifdef CONFIG_GMK70_POWER
    status.percentage = 100;
    assert(!gmk_lighting_battery_set(GMK_BATTERY_LEVEL, 55, true));
    gmk_lighting_poll();
    assert(last_pixels[16] == 80);
    assert(gmk_lighting_power_quiesce(true));
    for (unsigned i = 0; i < 108; ++i)
        assert(!last_pixels[i]);
    unsigned paused_writes = writes;
    memset(colors, 7, sizeof(colors));
    assert(!update_rgb(NULL, colors, 77));
    gmk_lighting_poll();
    assert(writes == paused_writes); /* Held and native animation cannot relight. */
    assert(gmk_lighting_power_quiesce(false));
    gmk_lighting_poll();
    assert(last_pixels[16] == 80);
    assert(!gmk_lighting_battery_release(55));
    gmk_lighting_poll();
    assert(last_pixels[16] == 7); /* Latest base survives the reversible pause. */
    write_result = -EIO;
    assert(!gmk_lighting_power_quiesce(true));
    write_result = 0;
    gmk_lighting_poll();
    assert(last_pixels[16] == 7);
    test_left     = false;
    physical_mode = 0;
    assert(!update_rgb(NULL, colors, 77) && last_pixels[0] == 7);
    assert(gmk_lighting_power_quiesce(true));
    for (unsigned i = 0; i < sizeof(last_pixels); ++i)
        assert(!last_pixels[i]);
    paused_writes = writes;
    assert(!update_rgb(NULL, colors, 77));
    gmk_lighting_poll();
    assert(writes == paused_writes);
    struct gmk_light_frame incoming = {0};
    uint8_t                rgb[3 * GMK_LIGHT_RIGHT], packet[16];
    memset(rgb, 7, sizeof(rgb));
    assert(gmk_light_queue(&incoming, rgb) && gmk_light_next(&incoming, packet));
    gmk_lighting_receive(packet);
    assert(!frame.receiving && !frame.ready);
    assert(gmk_lighting_power_quiesce(false));
    assert(!update_rgb(NULL, colors, 77) && last_pixels[0] == 7);
#endif
    puts("Held battery lighting: off-state visibility, current-base restore, status refresh and no caller mutation passed");
}

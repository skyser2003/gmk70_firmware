// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "battery_qmk.h"
#include "battery_indicator.h"
#include "battery_diagnostics.h"
#include "battery_output_probe.h"
#include "transactions.h"
#include "ws2812.h"

ws2812_led_t        ws2812_leds[WS2812_LED_COUNT];
static ws2812_led_t transferred[WS2812_LED_COUNT];
static uint32_t     now;
static bool         master = true, left = true, enabled = true, suspended, allowed = true;
static bool         wireless = true, charging, full, link = true, old_peer;
static uint8_t      percent = 70;
static unsigned     sends, flushes, diagnostic_frames;
static uint8_t      sent[5];

uint32_t timer_read32(void) {
    return now;
}
bool is_keyboard_master(void) {
    return master;
}
bool is_keyboard_left(void) {
    return left;
}
bool rgb_matrix_is_enabled(void) {
    return enabled;
}
bool rgb_matrix_get_suspend_state(void) {
    return suspended;
}
bool gmk70_battery_overlay_allowed(void) {
    return allowed;
}
battery_indicator_state_t gmk70_battery_local_state(void) {
    return battery_indicator_state(gmk70_battery_requested(true), gmk70_battery_requested(false), wireless, charging, full, percent);
}

void rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    // Model QMK's global-to-local split LED conversion.
    if (left && index >= 36) return;
    if (!left && index < 36) return;
    if (!left) index -= 36;
    assert(index >= 0 && index < (left ? 36 : 41));
    ws2812_leds[index] = (ws2812_led_t){.r = red, .g = green, .b = blue};
}
void rgb_matrix_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < 77; ++i)
        rgb_matrix_set_color(i, red, green, blue);
}
bool transaction_rpc_exec(int8_t id, uint8_t in_len, const void *in, uint8_t out_len, void *out) {
    assert(id == USER_SYNC_MMS && in_len == 5 && out_len == 5);
    memcpy(sent, in, 5);
    assert(sent[0] == BATTERY_INDICATOR_COMMAND);
    ++sends;
    ((uint8_t *)out)[0] = old_peer ? 0 : BATTERY_INDICATOR_COMMAND;
    return link;
}
void battery_diagnostics_render(battery_diag_render_t render) {
    (void)render;
}
void battery_output_probe_frame(bool held, bool left_half) {
    // Output observation is tested separately against the real SPI encoder.
}
void battery_diagnostics_frame(uint8_t rendered, uint8_t br, uint8_t bg, uint8_t bb, uint8_t ar, uint8_t ag, uint8_t ab) {
    assert(left);
    assert(ws2812_leds[33].r == ar && ws2812_leds[33].g == ag && ws2812_leds[33].b == ab);
    ++diagnostic_frames;
}
void __real_ws2812_flush(void) {
    memcpy(transferred, ws2812_leds, sizeof(transferred));
    ++flushes;
}
void __wrap_ws2812_flush(void);

static void frame(void) {
    rgb_matrix_set_color_all(12, 23, 34); // Animation/temporary blink just rendered.
    __wrap_ws2812_flush();
}
static void pixel(int global, uint8_t r, uint8_t g, uint8_t b) {
    int local = left ? global : global - 36;
    assert(transferred[local].r == r && transferred[local].g == g && transferred[local].b == b);
}

static void test_split_bar_and_release(void) {
    gmk70_battery_request(true, true);
    gmk70_battery_task();
    assert(sends == 1 && sent[2] == BATTERY_INDICATOR_LEVEL && sent[3] == 70);
    frame();
    for (int i = 5; i <= 9; ++i)
        pixel(i, 0, 150, 0);
    pixel(33, 0, 0, 0);
    assert(gmk70_battery_receive(sent + 1));
    master = left = false;
    percent       = 10; // Slave's local radio must not change the shared 70% bar.
    frame();
    pixel(39, 0, 150, 0);
    pixel(40, 0, 150, 0);
    pixel(41, 0, 0, 0);
    pixel(43, 0, 0, 0);
    master = left = true;
    gmk70_battery_request(true, false);
    now  = 50;
    link = false;
    gmk70_battery_task(); // Lost release; master is immediately restored.
    frame();
    pixel(5, 12, 23, 34);
    assert(sends == 2);
    now = 99;
    gmk70_battery_task();
    assert(sends == 2); // Failed link cannot stall every matrix scan.
    now  = 100;
    link = true;
    gmk70_battery_task();
    assert(sends == 3 && sent[2] == 0);
    assert(gmk70_battery_receive(sent + 1));
    master = left = false;
    frame();
    pixel(39, 12, 23, 34);
    master = left = true;
}

static void test_expiry_refresh_validation_and_wrap(void) {
    battery_indicator_sync_t  sync  = {0};
    battery_indicator_state_t state = battery_indicator_state(true, false, true, false, false, 100);
    uint8_t                   body[4];
    uint32_t                  start = UINT32_MAX - 200;
    assert(battery_indicator_peer(&sync, start).flags == 0);
    assert(battery_indicator_prepare(&sync, state, start, body));
    battery_indicator_acknowledge(&sync, state, start);
    assert(battery_indicator_receive(&sync, body, start));
    assert(!battery_indicator_prepare(&sync, state, start + 249, body));
    assert(battery_indicator_prepare(&sync, state, start + 250, body));
    assert(battery_indicator_peer(&sync, start + 1199).flags == BATTERY_INDICATOR_LEVEL);
    assert(battery_indicator_peer(&sync, start + 1200).flags == 0);
    const uint8_t invalid[][4] = {{2, 1, 70, 0}, {1, 8, 70, 0}, {1, 1, 101, 0}, {1, 1, 70, 1}, {1, 4, 70, 0}};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!battery_indicator_receive(&sync, invalid[i], start + 1199));
    assert(battery_indicator_peer(&sync, start + 1200).flags == 0);
    assert(battery_indicator_receive(&sync, body, start + 1250));
    assert(battery_indicator_peer(&sync, start + 1250).percent == 100);
    // Unsupported old peer acknowledgements must be retried.
    now            = 500;
    old_peer       = true;
    unsigned prior = sends;
    gmk70_battery_task();
    now += 50;
    gmk70_battery_task();
    assert(sends == prior + 2);
    old_peer = false;
}

static void test_charging_colors_and_frame_priority(void) {
    gmk70_battery_request(false, true);
    charging = true;
    frame();
    pixel(33, 80, 0, 0);
    pixel(5, 12, 23, 34);
    gmk70_battery_request(false, false);
    __wrap_ws2812_flush(); // An incremental effect need not repaint LED 33.
    pixel(33, 12, 23, 34);
    gmk70_battery_request(false, true);
    full = true;
    frame();
    pixel(33, 0, 80, 0);
    charging = false;
    frame();
    pixel(33, 12, 23, 34);
    charging = true;
    gmk70_battery_request(false, false);
    frame();
    pixel(33, 12, 23, 34);
    gmk70_battery_request(false, true);
    gmk70_battery_request(true, true);
    frame();
    pixel(33, 0, 0, 0); // The percentage bar has priority over charging.
    wireless = false;
    frame();
    pixel(33, 0, 80, 0); // USB hides only the percentage bar.
    enabled = false;
    frame();
    pixel(33, 12, 23, 34);
    enabled   = true;
    suspended = true;
    frame();
    pixel(33, 12, 23, 34);
    suspended = false;
    allowed   = false;
    frame();
    pixel(33, 12, 23, 34);
    allowed = true;
    assert(flushes > 0 && diagnostic_frames > 0);
}

static void test_percentage_boundaries(void) {
    wireless                = true;
    charging                = false;
    const uint8_t values[]  = {0, 9, 10, 20, 29, 30, 50, 70, 99, 100, 255};
    const int     indices[] = RGB_MATRIX_BAT_INDEX_MAP;
    for (unsigned v = 0; v < sizeof(values); ++v) {
        percent        = values[v];
        unsigned count = percent > 100 ? 10 : percent / 10;
        if (!count) count = 1;
        battery_indicator_state_t state  = gmk70_battery_local_state();
        uint8_t                   body[] = {1, state.flags, state.percent, 0};
        assert(gmk70_battery_receive(body));
        for (unsigned half = 0; half < 2; ++half) {
            master = left = half == 0;
            frame();
            for (unsigned i = half * 5; i < half * 5 + 5; ++i)
                pixel(indices[i], i < count && percent < 30 ? 150 : 0, i < count && percent >= 30 ? 150 : 0, 0);
        }
        master = left = true;
    }
}

int main(void) {
    test_split_bar_and_release();
    test_expiry_refresh_validation_and_wrap();
    test_charging_colors_and_frame_priority();
    test_percentage_boundaries();
    puts("Battery split, retries, expiry, charging colors and final driver frame passed");
}

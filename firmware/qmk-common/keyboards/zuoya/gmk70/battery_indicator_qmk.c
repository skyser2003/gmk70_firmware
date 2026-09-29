// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H
#include "battery_indicator.h"
#include "battery_diagnostics.h"
#include "transactions.h"
#include "ws2812.h"
#include <string.h>
#ifdef GMK70_BATTERY_DIAGNOSTICS
#    include "battery_output_probe.h"
#endif

static battery_indicator_sync_t sync_state;
static bool                     level_requested;
static bool                     charge_requested;

void gmk70_battery_request(bool level, bool pressed) {
    if (level)
        level_requested = pressed;
    else
        charge_requested = pressed;
}

bool gmk70_battery_requested(bool level) {
    return level ? level_requested : charge_requested;
}

void gmk70_battery_task(void) {
    if (!is_keyboard_master()) return;
    battery_indicator_state_t state       = gmk70_battery_local_state();
    uint8_t                   request[5]  = {BATTERY_INDICATOR_COMMAND};
    uint8_t                   response[5] = {0};
    if (battery_indicator_prepare(&sync_state, state, timer_read32(), request + 1) && transaction_rpc_exec(USER_SYNC_MMS, sizeof(request), request, sizeof(response), response) && response[0] == BATTERY_INDICATOR_COMMAND) {
        battery_indicator_acknowledge(&sync_state, state, timer_read32());
    }
}

bool gmk70_battery_receive(const uint8_t body[4]) {
    return battery_indicator_receive(&sync_state, body, timer_read32());
}

void __real_ws2812_flush(void);

// Run once per completed frame, after every animation and board indicator.
// Keep upstream's driver and RGB split-index conversion unmodified.
void __wrap_ws2812_flush(void) {
    extern ws2812_led_t ws2812_leds[WS2812_LED_COUNT];
    static ws2812_led_t base_frame[WS2812_LED_COUNT];
#ifdef GMK70_BATTERY_DIAGNOSTICS
    ws2812_led_t before = ws2812_leds[HS_MATRIX_BLINK_INDEX_BAT];
#endif
    uint8_t                            rendered = 0;
    const battery_indicator_renderer_t renderer = {.set_color = rgb_matrix_set_color, .set_all = rgb_matrix_set_color_all};
    if (gmk70_battery_overlay_allowed() && rgb_matrix_is_enabled() && !rgb_matrix_get_suspend_state()) {
        battery_indicator_state_t state = is_keyboard_master() ? gmk70_battery_local_state() : battery_indicator_peer(&sync_state, timer_read32());
        if (state.flags) memcpy(base_frame, ws2812_leds, sizeof(base_frame));
        rendered = battery_indicator_render(state, &renderer);
        if (rendered == BATTERY_INDICATOR_LEVEL) battery_diagnostics_render(BATTERY_DIAG_LEVEL_DRAW);
        if (rendered == BATTERY_INDICATOR_CHARGE && is_keyboard_left()) battery_diagnostics_render(BATTERY_DIAG_CHARGING_DRAW);
    }
#ifdef GMK70_BATTERY_DIAGNOSTICS
    battery_output_probe_frame(rendered == BATTERY_INDICATOR_CHARGE, is_keyboard_left());
    if (is_keyboard_left()) {
        ws2812_led_t pixel = ws2812_leds[HS_MATRIX_BLINK_INDEX_BAT];
        battery_diagnostics_frame(rendered, before.r, before.g, before.b, pixel.r, pixel.g, pixel.b);
    }
#endif
    __real_ws2812_flush();
    // The synchronous SPI transfer is complete. Restore the animation's own
    // buffer so incremental effects also recover on release.
    if (rendered) memcpy(ws2812_leds, base_frame, sizeof(base_frame));
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "rgb_matrix.h"
#include "lowpower.h"
#include "stock_battery.h"
#include "stock_power.h"
#include "wireless.h"

extern uint8_t pov;
extern bool    bat_full_flag;
bool           gmk70_stock_cable_flag(void);
void           __real_rgb_matrix_task(void);
void           __real_housekeeping_task(void);

static bool battery_indicator_requested(void) {
    return stock_battery_level_requested() || (stock_battery_charging_requested() && gmk70_stock_cable_flag());
}

void __wrap_housekeeping_task(void) {
    __real_housekeeping_task();
    // GUI changes and native split sync bypass the stock RGB toggle RPC.
    // Sleep/wake callbacks retain ownership outside normal operation.
    if (lpwr_get_state() != LPWR_NORMAL) return;
    bool enabled = (rgb_matrix_is_enabled() || battery_indicator_requested()) && !rgb_matrix_get_suspend_state();
    gpio_write_pin(LED_POWER_EN_PIN, enabled);
    gpio_write_pin(LED_POWER_EN2_PIN, enabled);
}

bool pre_process_record_kb(uint16_t keycode, keyrecord_t *record) {
    // Count physical keys once, before features consume or replay events.
    if (IS_EVENT(record->event)) {
        if (wireless_get_current_devs() == DEVS_USB) usb_remote_host();
        if (record->event.pressed) lpwr_activity_trigger();
        if (IS_KEYEVENT(record->event) && record->event.key.row < MATRIX_ROWS && record->event.key.col < MATRIX_COLS) stock_power_key(record->event.pressed);
    }
    return pre_process_record_user(keycode, record);
}

void __wrap_rgb_matrix_task(void) {
    static bool     indicator_frame;
    static uint32_t last_frame;
    bool            enabled = rgb_matrix_is_enabled();
    bool            awake   = lpwr_get_state() == LPWR_NORMAL && !rgb_matrix_get_suspend_state();

    if (!enabled && awake && battery_indicator_requested()) {
        // Both frameworks skip indicators when RGB is off. Render just the held
        // query, without changing its enable bit, effect or persistent config.
        // Pause the native NONE task so it cannot flush over this frame.
        if (!indicator_frame || timer_elapsed32(last_frame) >= RGB_MATRIX_LED_FLUSH_LIMIT) {
            gpio_write_pin(LED_POWER_EN_PIN, true);
            gpio_write_pin(LED_POWER_EN2_PIN, true);
            rgb_matrix_set_color_all(0, 0, 0);
            stock_battery_render(pov);
            stock_battery_render_charging(gmk70_stock_cable_flag(), bat_full_flag);
            rgb_matrix_update_pwm_buffers();
            last_frame      = timer_read32();
            indicator_frame = true;
        }
        return;
    }

    if (indicator_frame) {
        indicator_frame = false;
        // Sleep/suspend own the LED supply outside normal operation. When a
        // normal query ends, clear its last frame before returning to darkness.
        if (!enabled && awake) {
            rgb_matrix_set_color_all(0, 0, 0);
            rgb_matrix_update_pwm_buffers();
            gpio_write_pin(LED_POWER_EN_PIN, false);
            gpio_write_pin(LED_POWER_EN2_PIN, false);
        }
    }
    __real_rgb_matrix_task();
}

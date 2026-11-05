/* SPDX-License-Identifier: GPL-2.0-or-later */
#define DT_DRV_COMPAT zuoya_gmk70_rgb
#include "../wired/virtual_keys.h"
#include "lighting.h"
#include "lighting_frame.h"
#include "wb32_pixels.h"
#include "wired.h"
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_GMK70_CONTROLS
#    include "../controls/controls.h"
#    include <zmk/activity.h>
#endif
#ifdef CONFIG_GMK70_BATTERY
#    include "../battery/battery.h"
#    include <string.h>
#endif

static struct gmk_light_frame frame;
static struct k_spinlock      frame_lock;
static uint32_t               last_frame;
static int                    write_pixels(const uint8_t *rgb, size_t count);
#ifdef CONFIG_GMK70_POWER
static bool power_paused;
#endif
#ifdef CONFIG_GMK70_BATTERY
K_MUTEX_DEFINE(render_lock);
static uint8_t                 normal_pixels[3 * GMK_LIGHT_PIXELS];
static struct gmk_battery_view battery_view;
static uint16_t                displayed_status;
static uint32_t                displayed_at;
static bool                    display_dirty;
static bool                    last_split;
#    ifdef CONFIG_GMK70_CONTROLS
static uint32_t displayed_controls;
static bool     displayed_active;
static bool     displayed_warning;
#    endif

static uint16_t status_key(const struct gmk_battery_status *status) {
    return status->percentage | (status->valid ? 1U << 8 : 0) | (status->cable ? 1U << 9 : 0) | (status->full ? 1U << 10 : 0);
}

static int render_battery_locked(const struct gmk_battery_status *status) {
#    ifdef CONFIG_GMK70_POWER
    if (power_paused) {
        display_dirty = true;
        return 0;
    }
#    endif
    uint8_t pixels[sizeof(normal_pixels)];
    memcpy(pixels, normal_pixels, sizeof(pixels));
#    ifdef CONFIG_GMK70_CONTROLS
    uint32_t now     = k_uptime_get_32();
    uint32_t token   = gmk_controls_visual_token(now);
    bool     active  = zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
    bool     warning = active && gmk_battery_warning_active(status);
    gmk_controls_overlay(pixels, GMK_LIGHT_PIXELS, now, true, active);
    if (active) gmk_battery_warning_overlay(status, pixels, now);
#    endif
    gmk_battery_view_overlay(&battery_view, status, pixels);
    int result = write_pixels(pixels, GMK_LIGHT_LEFT);
    if (!result && gmk_split_available()) {
        k_spinlock_key_t key = k_spin_lock(&frame_lock);
        gmk_light_queue(&frame, pixels + 3 * GMK_LIGHT_LEFT);
        k_spin_unlock(&frame_lock, key);
    }
    display_dirty    = result != 0;
    displayed_status = status_key(status);
    displayed_at     = k_uptime_get_32();
#    ifdef CONFIG_GMK70_CONTROLS
    displayed_controls = token;
    displayed_active   = active;
    displayed_warning  = warning;
#    endif
    return result;
}

int gmk_lighting_battery_set(enum gmk_battery_indicator kind, uint32_t position, bool pressed) {
    if (k_is_in_isr()) return -EWOULDBLOCK;
    k_mutex_lock(&render_lock, K_FOREVER);
    int result = gmk_battery_view_set(&battery_view, kind, position, pressed);
    if (!result) display_dirty = true;
    k_mutex_unlock(&render_lock);
    return result;
}

int gmk_lighting_battery_release(uint32_t position) {
    if (position >= GMK_KEY_POSITIONS) return -EINVAL;
    if (k_is_in_isr()) return -EWOULDBLOCK;
    k_mutex_lock(&render_lock, K_FOREVER);
    if (gmk_battery_view_release(&battery_view, position)) display_dirty = true;
    k_mutex_unlock(&render_lock);
    return 0;
}

static void battery_refresh(void) {
    if (!gmk_wired_is_left()) return;
    k_mutex_lock(&render_lock, K_FOREVER);
    struct gmk_battery_status status;
    gmk_battery_get(&status);
    bool split = gmk_split_available();
#    ifdef CONFIG_GMK70_CONTROLS
    bool active  = zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
    bool warning = active && gmk_battery_warning_active(&status);
    if (displayed_controls != gmk_controls_visual_token(k_uptime_get_32()) || displayed_active != active || displayed_warning != warning || (warning && (uint32_t)(k_uptime_get_32() - displayed_at) >= 250U)) display_dirty = true;
#    endif
    if (split && !last_split) display_dirty = true;
    last_split = split;
    if (!split && gmk_battery_view_active(&battery_view)) {
        memset(&battery_view, 0, sizeof(battery_view));
        display_dirty = true;
    }
    /* A successful DMA transfer does not acknowledge reception by the LEDs.
     * Keep held indicators alive at the native animation cadence, including
     * while upstream RGB is off. One early frame after rail restoration must
     * not leave LEFT dark until another key/status change; peer frames already
     * have periodic replay. Releasing the last hold stops this local replay. */
    if (display_dirty || (gmk_battery_view_active(&battery_view) && (displayed_status != status_key(&status) || (uint32_t)(k_uptime_get_32() - displayed_at) >= 50U))) render_battery_locked(&status);
    k_mutex_unlock(&render_lock);
}
#endif

static int write_pixels(const uint8_t *rgb, size_t count) {
#ifdef CONFIG_GMK70_POWER
    k_mutex_lock(&render_lock, K_FOREVER);
    int result = power_paused ? 0 : gmk_pixels_write(rgb, count);
    k_mutex_unlock(&render_lock);
    return result;
#else
    return gmk_pixels_write(rgb, count);
#endif
}

bool gmk_lighting_power_quiesce(bool enabled) {
#ifdef CONFIG_GMK70_POWER
    k_mutex_lock(&render_lock, K_FOREVER);
    bool success = true;
    if (enabled && !power_paused) {
        uint8_t dark[3 * GMK_LIGHT_RIGHT] = {0};
        /* The synchronous driver drains DMA and removes both LED rails.
         * Each peer performs its own local step before advertising READY. */
        success = gmk_pixels_write(dark, gmk_wired_is_left() ? GMK_LIGHT_LEFT : GMK_LIGHT_RIGHT) == 0;
    }
    k_spinlock_key_t key = k_spin_lock(&frame_lock);
    power_paused         = enabled && success;
    gmk_light_reset(&frame);
    k_spin_unlock(&frame_lock, key);
    display_dirty = true;
    k_mutex_unlock(&render_lock);
    return success;
#else
    (void)enabled;
    return true;
#endif
}

void gmk_lighting_receive(const uint8_t packet[16]) {
    if (gmk_wired_is_left()) return;
    k_spinlock_key_t key = k_spin_lock(&frame_lock);
#ifdef CONFIG_GMK70_POWER
    if (!power_paused)
#endif
        gmk_light_accept(&frame, packet);
    k_spin_unlock(&frame_lock, key);
}

void gmk_lighting_wire_poll(const struct gmk_wire_io *io) {
#ifdef CONFIG_GMK70_RGB_LOCAL_ONLY
    (void)io;
    return;
#endif
    uint8_t          packet[16];
    k_spinlock_key_t key = k_spin_lock(&frame_lock);
#ifdef CONFIG_GMK70_POWER
    if (power_paused) {
        k_spin_unlock(&frame_lock, key);
        return;
    }
#endif
    /* Off submits exactly one upstream frame. Periodic replay also repairs
     * lost chunks and repaints a peripheral that rebooted independently. */
    if ((uint32_t)(k_uptime_get_32() - last_frame) >= 200) gmk_light_refresh(&frame);
    bool pending = gmk_light_next(&frame, packet);
    k_spin_unlock(&frame_lock, key);
    if (!pending) return;
    /* One chunk per scan, after the matrix response; never enqueue a burst
     * ahead of typing or hold an IRQ lock while waiting for UART. */
    bool success = gmk_protocol_lighting(io, packet);
    key          = k_spin_lock(&frame_lock);
    gmk_light_sent(&frame, packet, success);
    if (!frame.pending) last_frame = k_uptime_get_32();
    k_spin_unlock(&frame_lock, key);
}

void gmk_lighting_poll(void) {
#ifdef CONFIG_GMK70_BATTERY
    battery_refresh();
#endif
    k_spinlock_key_t key = k_spin_lock(&frame_lock);
    if (!gmk_split_available()) gmk_light_reset(&frame);
    bool ready = !gmk_wired_is_left() && gmk_light_claim(&frame);
    k_spin_unlock(&frame_lock, key);
    if (!ready) return;
    /* The claim prevents the serial callback changing rgb during encoding.
     * No wire/spin lock is held over the DMA waveform. */
    write_pixels(frame.rgb, GMK_LIGHT_RIGHT);
    key = k_spin_lock(&frame_lock);
    gmk_light_release(&frame);
    k_spin_unlock(&frame_lock, key);
}

static int update_rgb(const struct device *dev, struct led_rgb *pixels, size_t count) {
    ARG_UNUSED(dev);
    if (count != GMK_LIGHT_PIXELS) return -EINVAL;
    bool left = gmk_wired_is_left();
    /* The peripheral displays complete central frames while joined. Its own
     * upstream animation is used only in standalone switch mode. */
    if (!left && gmk_split_available()) return 0;
#ifdef CONFIG_GMK70_BATTERY
    if (left) {
        k_mutex_lock(&render_lock, K_FOREVER);
        for (unsigned i = 0; i < GMK_LIGHT_PIXELS; ++i) {
            normal_pixels[3 * i]     = pixels[i].r;
            normal_pixels[3 * i + 1] = pixels[i].g;
            normal_pixels[3 * i + 2] = pixels[i].b;
        }
        struct gmk_battery_status status;
        gmk_battery_get(&status);
        int result = render_battery_locked(&status);
        k_mutex_unlock(&render_lock);
        return result;
    }
#endif
    uint8_t  packed[3 * GMK_LIGHT_RIGHT];
    unsigned start = left ? 0 : GMK_LIGHT_LEFT;
    unsigned local = left ? GMK_LIGHT_LEFT : GMK_LIGHT_RIGHT;
    for (unsigned i = 0; i < local; ++i) {
        packed[3 * i]     = pixels[start + i].r;
        packed[3 * i + 1] = pixels[start + i].g;
        packed[3 * i + 2] = pixels[start + i].b;
    }
    int result = write_pixels(packed, local);
#ifdef CONFIG_GMK70_RGB_LOCAL_ONLY
    return result;
#endif
    if (result || !left || !gmk_split_available()) return result;
    for (unsigned i = 0; i < GMK_LIGHT_RIGHT; ++i) {
        packed[3 * i]     = pixels[GMK_LIGHT_LEFT + i].r;
        packed[3 * i + 1] = pixels[GMK_LIGHT_LEFT + i].g;
        packed[3 * i + 2] = pixels[GMK_LIGHT_LEFT + i].b;
    }
    k_spinlock_key_t key = k_spin_lock(&frame_lock);
    gmk_light_queue(&frame, packed);
    k_spin_unlock(&frame_lock, key);
    return 0;
}
static size_t length(const struct device *dev) {
    ARG_UNUSED(dev);
    return GMK_LIGHT_PIXELS;
}
static int init(const struct device *dev) {
    ARG_UNUSED(dev);
    return gmk_pixels_init();
}
static const struct led_strip_driver_api api = {.update_rgb = update_rgb, .length = length};
DEVICE_DT_INST_DEFINE(0, init, NULL, NULL, NULL, POST_KERNEL, 85, &api);

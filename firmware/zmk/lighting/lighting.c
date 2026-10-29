/* SPDX-License-Identifier: GPL-2.0-or-later */
#define DT_DRV_COMPAT zuoya_gmk70_rgb
#include "lighting.h"
#include "lighting_frame.h"
#include "wb32_pixels.h"
#include "wired.h"
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_GMK70_BATTERY
#    include "../battery/battery.h"
#    include <string.h>
#endif

static struct gmk_light_frame frame;
static struct k_spinlock      frame_lock;
static uint32_t               last_frame;
#ifdef CONFIG_GMK70_BATTERY
K_MUTEX_DEFINE(render_lock);
static uint8_t                 normal_pixels[3 * GMK_LIGHT_PIXELS];
static struct gmk_battery_view battery_view;
static uint16_t                displayed_status;
static bool                    display_dirty;
static bool                    last_wired;

static uint16_t status_key(const struct gmk_battery_status *status) {
    return status->percentage | (status->valid ? 1U << 8 : 0) | (status->cable ? 1U << 9 : 0) | (status->full ? 1U << 10 : 0);
}

static int render_battery_locked(const struct gmk_battery_status *status) {
    uint8_t pixels[sizeof(normal_pixels)];
    memcpy(pixels, normal_pixels, sizeof(pixels));
    gmk_battery_view_overlay(&battery_view, status, pixels);
    int result = gmk_pixels_write(pixels, GMK_LIGHT_LEFT);
    if (!result && gmk_wired_mode()) {
        k_spinlock_key_t key = k_spin_lock(&frame_lock);
        gmk_light_queue(&frame, pixels + 3 * GMK_LIGHT_LEFT);
        k_spin_unlock(&frame_lock, key);
    }
    display_dirty    = result != 0;
    displayed_status = status_key(status);
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
    if (position >= 72) return -EINVAL;
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
    bool wired = gmk_wired_mode();
    if (wired && !last_wired) display_dirty = true;
    last_wired = wired;
    if (!wired && gmk_battery_view_active(&battery_view)) {
        memset(&battery_view, 0, sizeof(battery_view));
        display_dirty = true;
    }
    if (display_dirty || (gmk_battery_view_active(&battery_view) && displayed_status != status_key(&status))) render_battery_locked(&status);
    k_mutex_unlock(&render_lock);
}
#endif

void gmk_lighting_receive(const uint8_t packet[16]) {
    if (gmk_wired_is_left()) return;
    k_spinlock_key_t key = k_spin_lock(&frame_lock);
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
    if (!gmk_wired_mode()) gmk_light_reset(&frame);
    bool ready = !gmk_wired_is_left() && gmk_light_claim(&frame);
    k_spin_unlock(&frame_lock, key);
    if (!ready) return;
    /* The claim prevents the serial callback changing rgb during encoding.
     * No wire/spin lock is held over the DMA waveform. */
    gmk_pixels_write(frame.rgb, GMK_LIGHT_RIGHT);
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
    if (!left && gmk_wired_mode()) return 0;
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
    int result = gmk_pixels_write(packed, local);
#ifdef CONFIG_GMK70_RGB_LOCAL_ONLY
    return result;
#endif
    if (result || !left || !gmk_wired_mode()) return result;
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

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "flip.h"
#include "core.h"
#include "../wired/wired.h"
#include "../studio/settings_backend.h"
#include <errno.h>
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

static struct gmk_flip_core core;
static struct k_spinlock    lock;
static struct k_work        save_work;
static uint32_t             changed_at, last_try;
static bool                 stored, saving, attempted;

int __real_raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed event);

static struct zmk_keycode_state_changed encoded_event(struct zmk_keycode_state_changed ev, uint32_t encoded) {
    ev.usage_page         = (encoded >> 16) & 0xff;
    ev.keycode            = encoded & 0xffff;
    ev.implicit_modifiers = encoded >> 24;
    return ev;
}
int __wrap_raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed ev) {
    if (!gmk_wired_is_left() || ev.usage_page != 7 || ev.keycode > 0xffff) return __real_raise_zmk_keycode_state_changed(ev);
    bool                   mac    = zmk_keymap_layer_active(2) || zmk_keymap_layer_active(3);
    uint32_t               input  = 0x70000 | ev.keycode | ((uint32_t)ev.implicit_modifiers << 24);
    k_spinlock_key_t       key    = k_spin_lock(&lock);
    struct gmk_flip_output output = gmk_flip_core_event(&core, input, ev.state, mac);
    k_spin_unlock(&lock, key);
    if (output.release_previous) {
        struct zmk_keycode_state_changed previous = encoded_event(ev, output.previous);
        previous.state                            = false;
        int result                                = __real_raise_zmk_keycode_state_changed(previous);
        if (result < 0) return result;
    }
    if (output.brightness) return zmk_rgb_underglow_change_brt(output.brightness);
    return output.emit ? __real_raise_zmk_keycode_state_changed(encoded_event(ev, output.key)) : 0;
}
int gmk_flip_toggle(void) {
    if (!gmk_wired_is_left()) return -ENODEV;
    k_spinlock_key_t key = k_spin_lock(&lock);
    core.enabled         = !core.enabled;
    changed_at           = k_uptime_get_32();
    k_spin_unlock(&lock, key);
    return 0;
}
static void save(struct k_work *work) {
    ARG_UNUSED(work);
    k_spinlock_key_t key   = k_spin_lock(&lock);
    uint8_t          value = core.enabled;
    k_spin_unlock(&lock, key);
    int result = gmk_zmk_settings_save_runtime("gmk_flip/state", &value, sizeof(value));
    key        = k_spin_lock(&lock);
    if (!result) stored = value != 0;
    saving = false;
    k_spin_unlock(&lock, key);
}
void gmk_flip_poll(bool writes_allowed) {
    if (!writes_allowed) return;
    uint32_t         now   = k_uptime_get_32();
    k_spinlock_key_t key   = k_spin_lock(&lock);
    bool             queue = core.enabled != stored && !saving && (uint32_t)(now - changed_at) >= 250 && (!attempted || (uint32_t)(now - last_try) >= 1000);
    if (queue) {
        saving = attempted = true;
        last_try           = now;
    }
    k_spin_unlock(&lock, key);
    if (queue) k_work_submit(&save_work);
}
static int load(const char *name, size_t len, settings_read_cb read, void *ctx) {
    if (strcmp(name, "state")) return -ENOENT;
    if (len != 1) return -EINVAL;
    uint8_t value;
    ssize_t result = read(ctx, &value, sizeof(value));
    if (result != 1) return result < 0 ? (int)result : -EINVAL;
    if (value > 1) return -EINVAL;
    k_spinlock_key_t key = k_spin_lock(&lock);
    core.enabled = stored = value != 0;
    k_spin_unlock(&lock, key);
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(gmk_flip, "gmk_flip", NULL, load, NULL, NULL);
static int flip_init(void) {
    core   = (struct gmk_flip_core){0};
    stored = saving = attempted = false;
    changed_at = last_try = 0;
    k_work_init(&save_work, save);
    return 0;
}
SYS_INIT(flip_init, POST_KERNEL, 88);

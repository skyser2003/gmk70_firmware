/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "flip.h"
#include "core.h"
#include "../wired/wired.h"
#include "../studio/bool_setting.h"
#include <errno.h>
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

static struct gmk_flip_core    core;
static struct k_spinlock       lock;
static struct gmk_bool_setting setting;
static struct k_mutex          dispatch_mutex;
struct dispatch_scope {
    struct dispatch_scope                  *previous;
    k_tid_t                                 owner;
    const struct zmk_keycode_state_changed *event;
    uint8_t                                 modifiers;
    bool                                    custom, canceled;
};
static struct dispatch_scope *dispatch;
static uint8_t                report_modifiers;

int __real_raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed event);
int __real_zmk_hid_implicit_modifiers_press(uint8_t modifiers);
int __real_zmk_hid_implicit_modifiers_release(void);
int __real_zmk_hid_press(uint32_t usage);
int __real_zmk_hid_release(uint32_t usage);
int zmk_hid_unregister_mods(uint8_t modifiers);

void gmk_flip_clear_begin(void) {
    (void)k_mutex_lock(&dispatch_mutex, K_FOREVER);
}
void gmk_flip_clear_end(void) {
    (void)k_mutex_unlock(&dispatch_mutex);
}

uint8_t gmk_flip_modifiers(void) {
    k_spinlock_key_t key       = k_spin_lock(&lock);
    uint8_t          modifiers = report_modifiers;
    k_spin_unlock(&lock, key);
    return modifiers;
}
void gmk_flip_clear(uint16_t page) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    gmk_flip_core_clear(&core, page);
    report_modifiers = gmk_flip_core_modifiers(&core);
    for (struct dispatch_scope *scope = dispatch; scope; scope = scope->previous) {
        scope->modifiers = report_modifiers;
        if (scope->custom && scope->event->state && scope->event->usage_page == page) scope->canceled = true;
    }
    k_spin_unlock(&lock, key);
}
#ifndef CONFIG_GMK70_RADIO
// Radio owns these wrappers when enabled; USB-only builds still cancel holds.
void __real_zmk_hid_keyboard_clear(void);
void __real_zmk_hid_consumer_clear(void);
void __wrap_zmk_hid_keyboard_clear(void) {
    gmk_flip_clear_begin();
    __real_zmk_hid_keyboard_clear();
    gmk_flip_clear(7);
    gmk_flip_clear_end();
}
void __wrap_zmk_hid_consumer_clear(void) {
    gmk_flip_clear_begin();
    __real_zmk_hid_consumer_clear();
    gmk_flip_clear(12);
    gmk_flip_clear_end();
}
#endif
static bool canceled_press(void) {
    k_spinlock_key_t key      = k_spin_lock(&lock);
    bool             canceled = dispatch && dispatch->owner == k_current_get() && dispatch->custom && dispatch->canceled;
    k_spin_unlock(&lock, key);
    return canceled;
}
int __wrap_zmk_hid_press(uint32_t usage) {
    // The dispatch mutex excludes concurrent clear. Native press itself has no
    // callbacks; a reentrant clear before this boundary must prevent insertion.
    return canceled_press() ? -ECANCELED : __real_zmk_hid_press(usage);
}
int __wrap_zmk_hid_release(uint32_t usage) {
    // A duplicate press can pre-release its target in the native listener.
    // A canceled press must not remove a newer nested event's same usage.
    return canceled_press() ? -ECANCELED : __real_zmk_hid_release(usage);
}
static bool custom_modifiers(int *changed) {
    uint8_t          canceled_explicit = 0;
    k_spinlock_key_t key               = k_spin_lock(&lock);
    bool             custom            = dispatch && dispatch->custom && dispatch->owner == k_current_get();
    if (custom) {
        *changed         = report_modifiers != dispatch->modifiers;
        report_modifiers = dispatch->modifiers;
        if (dispatch->canceled) canceled_explicit = dispatch->event->explicit_modifiers;
    }
    k_spin_unlock(&lock, key);
    // If clear followed native press, its key is already gone. Balance only
    // this event's explicit registration; nested native keys/modifiers survive.
    if (canceled_explicit) {
        int result = zmk_hid_unregister_mods(canceled_explicit);
        *changed   = result < 0 ? result : *changed || result > 0;
    }
    return custom;
}
int __wrap_zmk_hid_implicit_modifiers_press(uint8_t modifiers) {
    int changed = 0;
    return custom_modifiers(&changed) ? changed : __real_zmk_hid_implicit_modifiers_press(modifiers);
}
int __wrap_zmk_hid_implicit_modifiers_release(void) {
    int changed = 0;
    return custom_modifiers(&changed) ? changed : __real_zmk_hid_implicit_modifiers_release();
}

static int raise_custom(struct zmk_keycode_state_changed event, bool custom, uint8_t modifiers, uint8_t source) {
    // Native events execute synchronously in their emitting thread. The mutex
    // serializes adapter emitters; the stack scope supports recursive dispatch.
    struct dispatch_scope scope = {.owner = k_current_get(), .event = &event, .modifiers = modifiers, .custom = custom};
    k_spinlock_key_t      key   = k_spin_lock(&lock);
    if (custom && event.state && core.held[source] >> 8 == 4) {
        k_spin_unlock(&lock, key);
        return 0;
    }
    scope.modifiers &= gmk_flip_core_modifiers(&core);
    scope.previous = dispatch;
    dispatch       = &scope;
    k_spin_unlock(&lock, key);
    int result = __real_raise_zmk_keycode_state_changed(event);
    key        = k_spin_lock(&lock);
    dispatch   = scope.previous;
    if (dispatch && dispatch->owner == scope.owner) dispatch->modifiers = gmk_flip_core_modifiers(&core);
    k_spin_unlock(&lock, key);
    return result;
}

static struct zmk_keycode_state_changed encoded_event(struct zmk_keycode_state_changed ev, uint32_t encoded) {
    ev.usage_page         = (encoded >> 16) & 0xff;
    ev.keycode            = encoded & 0xffff;
    ev.implicit_modifiers = (uint8_t)(encoded >> 24);
    return ev;
}
int __wrap_raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed ev) {
    // Current keycode producers are thread/workqueue callbacks, never ISRs.
    int result = k_mutex_lock(&dispatch_mutex, K_FOREVER);
    if (result < 0) return result;
    if (!gmk_wired_is_left() || ev.usage_page != 7 || ev.keycode > 0xffff) {
        result = raise_custom(ev, false, 0, 24);
        goto done;
    }
    bool             mac             = zmk_keymap_layer_active(2) || zmk_keymap_layer_active(3);
    uint32_t         input           = 0x70000 | ev.keycode | ((uint32_t)ev.implicit_modifiers << 24);
    bool             enabled         = gmk_bool_setting_get(&setting);
    k_spinlock_key_t key             = k_spin_lock(&lock);
    core.enabled                     = enabled;
    struct gmk_flip_output output    = gmk_flip_core_event(&core, input, ev.state, mac);
    uint8_t                modifiers = gmk_flip_core_modifiers(&core);
    k_spin_unlock(&lock, key);
    if (output.release_previous) {
        struct zmk_keycode_state_changed previous = encoded_event(ev, output.previous);
        previous.state                            = false;
        result                                    = raise_custom(previous, output.previous_custom, output.remaining_modifiers, output.source);
        if (result < 0) goto done;
    }
    result = output.brightness ? zmk_rgb_underglow_change_brt(output.brightness) : output.emit ? raise_custom(encoded_event(ev, output.key), output.custom, modifiers, output.source) : 0;
done:
    (void)k_mutex_unlock(&dispatch_mutex);
    return result;
}
int gmk_flip_toggle(void) {
    if (!gmk_wired_is_left()) return -ENODEV;
    gmk_bool_setting_toggle(&setting);
    return 0;
}
void gmk_flip_poll(bool writes_allowed) {
    gmk_bool_setting_poll(&setting, writes_allowed);
}
bool gmk_flip_dirty(void) {
    return gmk_bool_setting_dirty(&setting);
}
static int load(const char *name, size_t len, settings_read_cb read, void *ctx) {
    if (strcmp(name, "state")) return -ENOENT;
    return gmk_bool_setting_load(&setting, len, read, ctx);
}
SETTINGS_STATIC_HANDLER_DEFINE(gmk_flip, "gmk_flip", NULL, load, NULL, NULL);
static int flip_init(void) {
    core = (struct gmk_flip_core){0};
    k_mutex_init(&dispatch_mutex);
    dispatch         = NULL;
    report_modifiers = 0;
    gmk_bool_setting_init(&setting, "gmk_flip/state", false);
    return 0;
}
SYS_INIT(flip_init, POST_KERNEL, 88);

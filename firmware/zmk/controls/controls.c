/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "controls.h"
#include "../runtime/idle.h"
#include "../wired/wired.h"
#include "../radio/core.h"
#ifdef CONFIG_GMK70_RADIO
#    include "../radio/radio.h"
#endif
#include "../../qmk-common/keyboards/zuoya/gmk70/stock_power.h"
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
#    include "../studio/settings_backend.h"
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zmk/endpoints.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

enum { GUI_LOCK = 1, MAC_MODE = 2, USB_SLEEP = 4, PREFERENCE_FLAGS = GUI_LOCK | MAC_MODE | USB_SLEEP };
struct controls_record {
    uint8_t version, flags, bt_slot, sleep_selector;
};
_Static_assert(sizeof(struct controls_record) == 4, "controls settings wire format must stay four bytes");

/* One record and one existing-system-workqueue job, with no owned stack or
 * pixel buffers. Stored tracks the exact successful snapshot, so a change
 * during a blocking save remains dirty and receives another debounced save. */
struct controls_state {
    struct controls_record value, stored;
    struct k_spinlock      lock;
    struct k_work          work;
    uint32_t               changed_at, last_try, radio_started, os_started;
    uint16_t               visual_generation;
    uint8_t                usb_leds, radio_leds, channel, connection;
    bool                   saving, attempted, os_feedback, restoring_bt;
};
static struct controls_state        preferences;
static const struct controls_record defaults = {.version = 1, .bt_slot = GMK_HOST_BT1, .sleep_selector = 7};

void gmk_controls_reset(void) {
    k_spinlock_key_t key = k_spin_lock(&preferences.lock);
    preferences.value = preferences.stored = defaults;
    preferences.restoring_bt = preferences.os_feedback = false;
    ++preferences.visual_generation;
    k_spin_unlock(&preferences.lock, key);
#ifdef CONFIG_GMK70_RADIO
    (void)gmk_radio_restore_last_bt(GMK_HOST_BT1);
#endif
}

static struct controls_record current(void) {
    k_spinlock_key_t       key   = k_spin_lock(&preferences.lock);
    struct controls_record value = preferences.value;
    k_spin_unlock(&preferences.lock, key);
    return value;
}

static bool update_flag(uint8_t flag, bool enabled, bool toggle) {
    k_spinlock_key_t key = k_spin_lock(&preferences.lock);
    uint8_t          old = preferences.value.flags;
    if (toggle) enabled = (old & flag) == 0;
    preferences.value.flags = enabled ? (uint8_t)(old | flag) : (uint8_t)(old & (uint8_t)~flag);
    bool changed            = preferences.value.flags != old;
    if (changed) {
        preferences.changed_at = k_uptime_get_32();
        ++preferences.visual_generation;
        if (flag == MAC_MODE) {
            preferences.os_started  = preferences.changed_at;
            preferences.os_feedback = true;
        }
    }
    k_spin_unlock(&preferences.lock, key);
    if (changed) {
        gmk_service_notify();
        gmk_scan_notify();
    }
    return changed;
}

bool gmk_controls_gui_locked(void) {
    return (current().flags & GUI_LOCK) != 0;
}
static int set_gui_lock(bool enabled, bool toggle) {
    if (!gmk_wired_is_left()) return -ENODEV;
    if (update_flag(GUI_LOCK, enabled, toggle)) (void)zmk_endpoint_send_report(7);
    return 0;
}
int gmk_controls_gui_lock_set(bool locked) {
    return set_gui_lock(locked, false);
}
int gmk_controls_gui_lock_toggle(void) {
    return set_gui_lock(false, true);
}
bool gmk_controls_mac_mode(void) {
    return (current().flags & MAC_MODE) != 0;
}
int gmk_controls_mac_set(bool mac) {
    if (!gmk_wired_is_left()) return -ENODEV;
    /* Do not use layer_to: that would clear other native layers, including
     * held Fn layers. Native keymap retains its press-time release bindings. */
    int result = mac ? zmk_keymap_layer_activate(2, false) : zmk_keymap_layer_deactivate(2, false);
    if (result < 0) return result;
    if (zmk_keymap_layer_active(2) != mac) return -EACCES;
    if (update_flag(MAC_MODE, mac, false)) (void)zmk_endpoint_send_report(7);
    return 0;
}
int gmk_controls_mac_toggle(void) {
    return gmk_controls_mac_set(!gmk_controls_mac_mode());
}
uint8_t gmk_controls_bt_slot(void) {
    return current().bt_slot;
}
uint8_t gmk_controls_sleep_selector(void) {
    return current().sleep_selector;
}
uint32_t gmk_controls_sleep_timeout_ms(void) {
    return stock_power_timeout(gmk_controls_sleep_selector());
}
bool gmk_controls_usb_sleep(void) {
    return (current().flags & USB_SLEEP) != 0;
}
int gmk_controls_usb_sleep_set(bool enabled) {
    if (!gmk_wired_is_left()) return -ENODEV;
    (void)update_flag(USB_SLEEP, enabled, false);
    return 0;
}
static int set_selector(bool bluetooth, uint8_t value) {
    if (!gmk_wired_is_left()) return -ENODEV;
    if (bluetooth ? value < GMK_HOST_BT1 || value > GMK_HOST_BT5 : value > 7) return -EINVAL;
    k_spinlock_key_t key     = k_spin_lock(&preferences.lock);
    uint8_t         *field   = bluetooth ? &preferences.value.bt_slot : &preferences.value.sleep_selector;
    bool             changed = *field != value;
    if (*field != value) {
        *field                 = value;
        preferences.changed_at = k_uptime_get_32();
    }
    k_spin_unlock(&preferences.lock, key);
    if (changed) gmk_service_notify();
    return 0;
}
int gmk_controls_bt_slot_set(uint8_t slot) {
    return set_selector(true, slot);
}
int gmk_controls_sleep_selector_set(uint8_t selector) {
    return set_selector(false, selector);
}

/* Preserve the linked integration witness under LTO. */
__attribute__((noipa)) uint8_t gmk_controls_project_modifiers(uint8_t modifiers) {
    uint8_t flags = current().flags;
    return (flags & (GUI_LOCK | MAC_MODE)) == GUI_LOCK ? (uint8_t)(modifiers & (uint8_t)~0x88U) : modifiers;
}

static bool dirty_locked(void) {
    return memcmp(&preferences.value, &preferences.stored, sizeof(preferences.value)) != 0;
}
bool gmk_controls_dirty(void) {
    k_spinlock_key_t key   = k_spin_lock(&preferences.lock);
    bool             dirty = preferences.saving || dirty_locked();
    k_spin_unlock(&preferences.lock, key);
    return dirty;
}
static void save_preferences(struct k_work *work) {
    ARG_UNUSED(work);
    struct controls_record value = current();
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    int result = gmk_zmk_settings_save_runtime("gmk_controls/state", &value, sizeof(value));
#else
    int result = -ENOTSUP;
#endif
    k_spinlock_key_t key = k_spin_lock(&preferences.lock);
    if (!result) preferences.stored = value;
    preferences.saving = false;
    k_spin_unlock(&preferences.lock, key);
    gmk_service_notify();
}
/* The image checker uses this entry to require all basic-feature hooks. */
__attribute__((noipa)) void gmk_controls_poll(bool writes_allowed) {
    if (!writes_allowed || !gmk_wired_is_left()) return;
    uint32_t         now   = k_uptime_get_32();
    k_spinlock_key_t key   = k_spin_lock(&preferences.lock);
    bool             queue = dirty_locked() && !preferences.saving && (uint32_t)(now - preferences.changed_at) >= 250U && (!preferences.attempted || (uint32_t)(now - preferences.last_try) >= 1000U);
    if (queue) {
        preferences.saving = preferences.attempted = true;
        preferences.last_try                       = now;
    }
    k_spin_unlock(&preferences.lock, key);
    if (queue && k_work_submit(&preferences.work) < 0) {
        key                = k_spin_lock(&preferences.lock);
        preferences.saving = false;
        k_spin_unlock(&preferences.lock, key);
    }
}

void gmk_controls_radio_state(uint8_t selected, uint8_t connection) {
    if (!gmk_wired_is_left() || selected > GMK_HOST_DONGLE || connection > GMK_RADIO_REJECTED) return;
    bool bluetooth_mode = true;
#ifdef CONFIG_GMK70_RADIO
    bluetooth_mode = gmk_host_switch() == 2;
#endif
    k_spinlock_key_t key     = k_spin_lock(&preferences.lock);
    bool             changed = preferences.channel != selected || preferences.connection != connection;
    /* The service owner can publish its old default host after settings load
     * and before the asynchronous restore request executes. Suppress that
     * stale selection until the loaded slot is seen or the selector leaves BT. */
    if (preferences.restoring_bt && (selected == preferences.value.bt_slot || !bluetooth_mode)) preferences.restoring_bt = false;
    if (bluetooth_mode && !preferences.restoring_bt && preferences.channel != selected && selected >= GMK_HOST_BT1 && selected <= GMK_HOST_BT5 && preferences.value.bt_slot != selected) {
        preferences.value.bt_slot = selected;
        preferences.changed_at    = k_uptime_get_32();
    }
    if (preferences.channel != selected || preferences.connection != connection) {
        if (preferences.channel != selected || connection != GMK_RADIO_CONNECTED) preferences.radio_leds = 0;
        preferences.channel       = selected;
        preferences.connection    = connection;
        preferences.radio_started = k_uptime_get_32();
        ++preferences.visual_generation;
    }
    k_spin_unlock(&preferences.lock, key);
    if (changed) {
        gmk_scan_notify();
        gmk_service_notify();
    }
}
void gmk_controls_radio_leds(uint8_t indicators) {
    if (!gmk_wired_is_left()) return;
    k_spinlock_key_t key     = k_spin_lock(&preferences.lock);
    bool             changed = preferences.radio_leds != indicators;
    if (preferences.radio_leds != indicators) {
        preferences.radio_leds = indicators;
        ++preferences.visual_generation;
    }
    k_spin_unlock(&preferences.lock, key);
    if (changed) gmk_scan_notify();
}

/* 0 restores ordinary lighting; 1 and 2 explicitly draw the on/off phases. */
static uint8_t blink_phase(uint32_t elapsed, uint32_t interval, uint32_t duration) {
    return duration && elapsed >= duration ? 0 : (uint8_t)(1U + elapsed / interval % 2U);
}
static uint8_t radio_phase(uint32_t now) {
    if (preferences.channel == GMK_HOST_USB) return 0;
    return blink_phase(now - preferences.radio_started, preferences.connection == GMK_RADIO_PAIRING ? 200U : 500U, preferences.connection == GMK_RADIO_CONNECTED ? 1000U : 0U);
}
static uint8_t os_phase(uint32_t now) {
    return preferences.os_feedback ? blink_phase(now - preferences.os_started, 250, 1500) : 0;
}
uint32_t gmk_controls_visual_token(uint32_t now) {
    if (!gmk_wired_is_left()) return 0;
    k_spinlock_key_t key   = k_spin_lock(&preferences.lock);
    uint32_t         token = (uint32_t)preferences.visual_generation << 4 | (uint32_t)os_phase(now) << 2 | radio_phase(now);
    k_spin_unlock(&preferences.lock, key);
    return token;
}
uint32_t gmk_controls_visual_delay(uint32_t now) {
    if (!gmk_wired_is_left()) return UINT32_MAX;
    k_spinlock_key_t key   = k_spin_lock(&preferences.lock);
    uint32_t         delay = UINT32_MAX;
    if (radio_phase(now)) {
        uint32_t interval = preferences.connection == GMK_RADIO_PAIRING ? 200U : 500U;
        delay             = interval - (now - preferences.radio_started) % interval;
    }
    if (os_phase(now)) {
        uint32_t next = 250U - (now - preferences.os_started) % 250U;
        if (next < delay) delay = next;
    }
    k_spin_unlock(&preferences.lock, key);
    return delay;
}
static void color(uint8_t *rgb, size_t count, unsigned index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= count) return;
    rgb[3 * index]     = r;
    rgb[3 * index + 1] = g;
    rgb[3 * index + 2] = b;
}
void gmk_controls_overlay(uint8_t *rgb, size_t count, uint32_t now, bool left, bool enabled) {
    if (!rgb || !left || !enabled || !gmk_wired_is_left()) return;
    k_spinlock_key_t key   = k_spin_lock(&preferences.lock);
    uint8_t          flags = preferences.value.flags;
    uint8_t          leds  = preferences.channel == GMK_HOST_USB ? preferences.usb_leds : preferences.radio_leds;
    if (leds & 2U) color(rgb, count, 18, 80, 80, 80);
    if ((flags & (GUI_LOCK | MAC_MODE)) == GUI_LOCK) color(rgb, count, 73, 80, 80, 80);
    uint8_t phase = os_phase(now);
    if (phase) {
        uint8_t white = phase == 1 ? 80 : 0;
        color(rgb, count, flags & MAC_MODE ? 20U : 19U, white, white, white);
    }
    phase = radio_phase(now);
    if (phase) {
        uint8_t  channel   = preferences.channel;
        unsigned index     = channel <= GMK_HOST_BT3 ? 15U - channel : channel <= GMK_HOST_BT5 ? 37U + channel : 11U;
        uint8_t  intensity = phase == 1 ? 80 : 0;
        color(rgb, count, index, 0, channel == GMK_HOST_DONGLE ? intensity : 0, channel == GMK_HOST_DONGLE ? 0 : intensity);
    }
    k_spin_unlock(&preferences.lock, key);
}

static int control_listener(const zmk_event_t *event) {
    if (!gmk_wired_is_left()) return ZMK_EV_EVENT_BUBBLE;
    const struct zmk_hid_indicators_changed *leds = as_zmk_hid_indicators_changed(event);
    if (leds) {
        k_spinlock_key_t key     = k_spin_lock(&preferences.lock);
        bool             changed = preferences.usb_leds != leds->indicators;
        if (changed) {
            preferences.usb_leds = leds->indicators;
            ++preferences.visual_generation;
        }
        k_spin_unlock(&preferences.lock, key);
        if (changed) gmk_scan_notify();
    }
    const struct zmk_layer_state_changed *layer = as_zmk_layer_state_changed(event);
    if (layer && layer->layer == 2 && update_flag(MAC_MODE, layer->state, false)) (void)zmk_endpoint_send_report(7);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(gmk_controls, control_listener);
ZMK_SUBSCRIPTION(gmk_controls, zmk_hid_indicators_changed);
ZMK_SUBSCRIPTION(gmk_controls, zmk_layer_state_changed);

#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
static int load_preferences(const char *name, size_t len, settings_read_cb read, void *ctx) {
    if (strcmp(name, "state")) return -ENOENT;
    if (!gmk_wired_is_left()) return -ENODEV;
    if (len != sizeof(struct controls_record)) return -EINVAL;
    struct controls_record value;
    ssize_t                result = read(ctx, &value, sizeof(value));
    if (result != (ssize_t)sizeof(value)) return result < 0 ? (int)result : -EINVAL;
    if (value.version != 1 || (value.flags & (uint8_t)~PREFERENCE_FLAGS) || value.bt_slot < GMK_HOST_BT1 || value.bt_slot > GMK_HOST_BT5 || value.sleep_selector > 7) return -EINVAL;
    k_spinlock_key_t key = k_spin_lock(&preferences.lock);
    preferences.value = preferences.stored = value;
#    ifdef CONFIG_GMK70_RADIO
    preferences.restoring_bt = true;
#    endif
    ++preferences.visual_generation;
    k_spin_unlock(&preferences.lock, key);
    return 0;
}
static int commit_preferences(void) {
    if (!gmk_wired_is_left()) return 0;
    int result = gmk_controls_mac_set(gmk_controls_mac_mode());
    if (result) return result;
#    ifdef CONFIG_GMK70_RADIO
    uint8_t slot = gmk_controls_bt_slot();
    result       = gmk_radio_restore_last_bt(slot);
    if (result) return result;
    bool             bluetooth_mode = gmk_host_switch() == 2;
    k_spinlock_key_t key            = k_spin_lock(&preferences.lock);
    preferences.restoring_bt        = bluetooth_mode;
    k_spin_unlock(&preferences.lock, key);
    if (bluetooth_mode) {
        struct gmk_radio_status radio;
        gmk_radio_get(&radio);
        if (radio.selected != slot) return gmk_radio_select(slot, false);
    }
#    endif
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(gmk_controls, "gmk_controls", NULL, load_preferences, commit_preferences, NULL);
#endif
static int controls_init(void) {
    preferences = (struct controls_state){.value = defaults, .stored = defaults};
    k_work_init(&preferences.work, save_preferences);
    return 0;
}
SYS_INIT(controls_init, POST_KERNEL, 88);

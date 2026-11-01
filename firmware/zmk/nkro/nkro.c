/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nkro.h"
#include "../studio/bool_setting.h"
#include "../wired/wired.h"
#include "../../qmk-common/drivers/wireless/radio_keys.h"
#ifdef CONFIG_GMK70_RADIO
#    include "../radio/radio.h"
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#ifdef CONFIG_GMK70_FUNCTION_FLIP
#    include "../function_flip/flip.h"
#endif

#if !defined(CONFIG_ZMK_HID_REPORT_TYPE_NKRO) || CONFIG_ZMK_HID_REPORT_TYPE_NKRO != 1
#    error "GMK70 NKRO requires the native NKRO keyboard report"
#endif

static struct gmk_bool_setting        setting;
static struct gmk70_radio_keys        slots;
static struct k_spinlock              projection_lock;
static struct zmk_hid_keyboard_report limited;

struct zmk_hid_keyboard_report *__real_zmk_hid_get_keyboard_report(void);
struct zmk_hid_keyboard_report *__wrap_zmk_hid_get_keyboard_report(void) {
    struct zmk_hid_keyboard_report *native  = __real_zmk_hid_get_keyboard_report();
    bool                            enabled = gmk_nkro_enabled();
#ifndef CONFIG_GMK70_FUNCTION_FLIP
    if (enabled) return native;
#else
    uint8_t modifiers = gmk_flip_modifiers();
#endif
    k_spinlock_key_t key = k_spin_lock(&projection_lock);
    limited              = *native;
#ifdef CONFIG_GMK70_FUNCTION_FLIP
    limited.body.modifiers |= modifiers;
#endif
    if (!enabled) {
        uint8_t keyboard[8], overflow[14];
        gmk70_radio_keys_encode(&slots, native->body.modifiers, native->body.keys, sizeof(native->body.keys), keyboard, overflow);
        memset(limited.body.keys, 0, sizeof(limited.body.keys));
        for (unsigned i = 2; i < sizeof(keyboard); ++i) {
            unsigned usage = keyboard[i];
            if (usage && usage / 8 < sizeof(limited.body.keys)) limited.body.keys[usage / 8] |= (uint8_t)(1U << (usage % 8));
        }
    }
    k_spin_unlock(&projection_lock, key);
    return &limited;
}

#ifdef CONFIG_ZMK_USB_BOOT
static zmk_hid_boot_report_t boot;
zmk_hid_boot_report_t       *__wrap_zmk_hid_get_boot_report(void) {
    // The pinned native held counter survives clear and can underflow after
    // duplicate bindings. Derive boot output from its authoritative bitmap.
    const struct zmk_hid_keyboard_report *report = __real_zmk_hid_get_keyboard_report();
    boot                                         = (zmk_hid_boot_report_t){.modifiers = report->body.modifiers};
#    ifdef CONFIG_GMK70_FUNCTION_FLIP
    boot.modifiers |= gmk_flip_modifiers();
#    endif
    unsigned count = 0;
    for (unsigned usage = 1; usage < sizeof(report->body.keys) * 8; ++usage) {
        if (!(report->body.keys[usage / 8] & (1U << (usage % 8)))) continue;
        if (count == HID_BOOT_KEY_LEN) {
            memset(boot.keys, HID_ERROR_ROLLOVER, sizeof(boot.keys));
            break;
        }
        boot.keys[count++] = (uint8_t)usage;
    }
    return &boot;
}
#endif

bool gmk_nkro_enabled(void) {
    return gmk_bool_setting_get(&setting);
}
int gmk_nkro_set(bool enabled) {
    if (!gmk_wired_is_left()) return -ENODEV;
    if (!gmk_bool_setting_set(&setting, enabled)) return 0;
#ifdef CONFIG_GMK70_RADIO
    // Retire queued/overflow state before publishing a new rollover snapshot.
    gmk_radio_release(false);
    gmk_radio_release(true);
#endif
    // Both USB modes use the same descriptor and a complete bitmap snapshot.
    // Keep physical state intact so enabling NKRO restores all still-held keys.
    (void)zmk_endpoint_send_report(7);
    (void)zmk_endpoint_send_report(12);
    return 0;
}
int gmk_nkro_toggle(void) {
    return gmk_nkro_set(!gmk_nkro_enabled());
}
void gmk_nkro_poll(bool writes_allowed) {
    gmk_bool_setting_poll(&setting, writes_allowed);
}

#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
static int load(const char *name, size_t len, settings_read_cb read, void *ctx) {
    return strcmp(name, "state") ? -ENOENT : gmk_bool_setting_load(&setting, len, read, ctx);
}
SETTINGS_STATIC_HANDLER_DEFINE(gmk_nkro, "gmk_nkro", NULL, load, NULL, NULL);
#endif
static int nkro_init(void) {
    gmk_bool_setting_init(&setting, "gmk_nkro/state", true);
    slots = (struct gmk70_radio_keys){0};
    return 0;
}
SYS_INIT(nkro_init, POST_KERNEL, 88);

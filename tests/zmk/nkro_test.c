/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/zmk/nkro/nkro.c"

uint32_t                              radio_test_time;
unsigned                              radio_test_locked;
static bool                           left = true;
static struct zmk_hid_keyboard_report native;
static unsigned                       releases, sends;
static unsigned                       phase;

#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
static ssize_t read_state(void *ctx, void *data, size_t size) {
    assert(size == 1);
    memcpy(data, ctx, size);
    return (ssize_t)size;
}
#endif
bool gmk_wired_is_left(void) {
    return left;
}
struct zmk_hid_keyboard_report *__real_zmk_hid_get_keyboard_report(void) {
    return &native;
}
void gmk_radio_release(bool consumer) {
    assert(!radio_test_locked && phase < 2 && consumer == (phase == 1));
    ++phase;
    ++releases;
}
int zmk_endpoint_send_report(uint16_t page) {
    assert(!radio_test_locked && phase >= 2 && phase < 4);
    assert(page == (phase == 2 ? 7 : 12));
    phase = (phase + 1) % 4;
    ++sends;
    return -ENOTCONN; /* A disconnected endpoint must not reject the setting. */
}
int gmk_zmk_settings_save_runtime(const char *name, const void *data, size_t size) {
    (void)name;
    (void)data;
    (void)size;
    assert(false && "Runtime persistence must be deferred and gated");
    return -EIO;
}
static void key(unsigned usage, bool down) {
    const uint8_t mask = (uint8_t)(1U << (usage % 8));
    if (down)
        native.body.keys[usage / 8] |= mask;
    else
        native.body.keys[usage / 8] &= (uint8_t)~mask;
}
static bool has(const struct zmk_hid_keyboard_report *report, unsigned usage) {
    return (report->body.keys[usage / 8] & (1U << (usage % 8))) != 0;
}
static unsigned count(const struct zmk_hid_keyboard_report *report) {
    unsigned held = 0;
    for (unsigned usage = 1; usage < sizeof(report->body.keys) * 8; ++usage)
        if (has(report, usage)) ++held;
    return held;
}
int main(void) {
    assert(!registered_init() && gmk_nkro_enabled());
    native.report_id      = 1;
    native.body.modifiers = 0x28;
    for (unsigned usage = 4; usage < 14; ++usage)
        key(usage, true);
    assert(count(__wrap_zmk_hid_get_keyboard_report()) == 10);
    assert(!gmk_nkro_set(false) && !gmk_nkro_enabled());
    const struct zmk_hid_keyboard_report *limited = __wrap_zmk_hid_get_keyboard_report();
    assert(count(limited) == 6 && limited->report_id == 1 && limited->body.modifiers == 0x28);
    for (unsigned usage = 4; usage < 10; ++usage)
        assert(has(limited, usage));
    assert(count(&native) == 10); /* Projection must not destroy physical state. */
    assert(releases == 2 && sends == 2 && phase == 0);
    assert(!gmk_nkro_set(false) && releases == 2);
    key(4, false);
    key(7, false);
    limited = __wrap_zmk_hid_get_keyboard_report();
    assert(count(limited) == 6 && !has(limited, 4) && !has(limited, 7));
    assert(has(limited, 5) && has(limited, 9) && has(limited, 10) && has(limited, 11));
    assert(!gmk_nkro_toggle() && gmk_nkro_enabled());
    assert(count(__wrap_zmk_hid_get_keyboard_report()) == 8 && releases == 4);
    assert(!gmk_nkro_set(false));
    key(0x73, true);
    assert(has(__wrap_zmk_hid_get_keyboard_report(), 0x73));
    assert(!gmk_nkro_set(true) && has(__wrap_zmk_hid_get_keyboard_report(), 0x73));
    left = false;
    assert(gmk_nkro_toggle() == -ENODEV && gmk_nkro_set(false) == -ENODEV);
    assert(gmk_nkro_enabled() && sends == 8 && phase == 0);
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    left = true;
    assert(!registered_init() && gmk_nkro_enabled());
    uint8_t saved = 0;
    assert(!registered_set("state", 1, read_state, &saved) && !gmk_nkro_enabled());
    assert(count(__wrap_zmk_hid_get_keyboard_report()) == 6);
    saved = 2;
    assert(registered_set("state", 1, read_state, &saved) == -EINVAL && !gmk_nkro_enabled());
    assert(registered_set("other", 1, read_state, &saved) == -ENOENT);
#endif
    puts("NKRO: default on, six-key projection, native state, modifiers, releases and role gate passed");
}

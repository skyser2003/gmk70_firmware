/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/zmk/function_flip/flip.c"

uint32_t                                radio_test_time;
unsigned                                radio_test_locked;
static bool                             left      = true, mac, toggle_during_save;
static bool                             usb_ready = true;
static unsigned                         writes, raised;
static int                              save_result, brightness;
static uint8_t                          persisted;
static struct zmk_keycode_state_changed seen[8];
bool                                    gmk_wired_is_left(void) {
    return left;
}
bool zmk_keymap_layer_active(uint8_t layer) {
    return mac && layer == 2;
}
int settings_save_one(const char *name, const void *value, size_t size) {
    assert(!radio_test_locked && strcmp(name, "gmk_flip/state") == 0 && size == 1);
    ++writes;
    if (toggle_during_save) {
        toggle_during_save = false;
        assert(gmk_flip_toggle() == 0);
    }
    if (!save_result) persisted = *(const uint8_t *)value;
    return save_result;
}
int gmk_zmk_settings_save_runtime(const char *name, const void *value, size_t size) {
    return usb_ready ? settings_save_one(name, value, size) : -ENOTSUP;
}
int zmk_rgb_underglow_change_brt(int direction) {
    assert(!radio_test_locked);
    brightness += direction;
    return 0;
}
int __real_raise_zmk_keycode_state_changed(struct zmk_keycode_state_changed ev) {
    assert(!radio_test_locked && raised < 8);
    seen[raised++] = ev;
    return 17;
}
static void start(void) {
    usb_ready       = true;
    radio_test_time = 0;
    left            = true;
    mac = toggle_during_save = false;
    writes = raised = 0;
    brightness = save_result = 0;
    persisted                = 0;
    assert(registered_init() == 0);
}
static void run_work(void) {
    assert(save_work.pending);
    save_work.pending = false;
    save_work.handler(&save_work);
}
static void persistence_is_deferred_coalesced_gated_and_retried(void) {
    start();
    assert(gmk_flip_toggle() == 0);
    radio_test_time = 250;
    gmk_flip_poll(false);
    assert(!save_work.pending && writes == 0);
    gmk_flip_poll(true);
    assert(save_work.pending && writes == 0);
    save_result = -EBUSY;
    run_work();
    assert(writes == 1 && persisted == 0);
    radio_test_time = 1249;
    gmk_flip_poll(true);
    assert(!save_work.pending);
    radio_test_time = 1250;
    gmk_flip_poll(true);
    save_result = 0;
    run_work();
    assert(writes == 2 && persisted == 1);
    radio_test_time = 2500;
    gmk_flip_poll(true);
    assert(!save_work.pending);
    assert(gmk_flip_toggle() == 0);
    assert(gmk_flip_toggle() == 0);
    radio_test_time = 4000;
    gmk_flip_poll(true);
    assert(!save_work.pending);
    assert(gmk_flip_toggle() == 0);
    radio_test_time = 4500;
    gmk_flip_poll(true);
    toggle_during_save = true;
    run_work();
    assert(persisted == 0);
    radio_test_time = 5500;
    gmk_flip_poll(true);
    run_work();
    assert(persisted == 1);
    left = false;
    assert(gmk_flip_toggle() == -ENODEV);
}
static ssize_t read_byte(void *ctx, void *out, size_t size) {
    assert(size == 1);
    memcpy(out, ctx, 1);
    return 1;
}
static ssize_t short_read(void *ctx, void *out, size_t size) {
    (void)ctx;
    (void)out;
    (void)size;
    return 0;
}
static void native_events_and_strict_settings_load(void) {
    start();
    uint8_t loaded = 1;
    assert(registered_set("state", 1, read_byte, &loaded) == 0);
    assert(registered_set("state", 2, read_byte, &loaded) == -EINVAL);
    loaded = 2;
    assert(registered_set("state", 1, read_byte, &loaded) == -EINVAL);
    assert(registered_set("state", 1, short_read, &loaded) == -EINVAL);
    assert(registered_set("unknown", 1, read_byte, &loaded) == -ENOENT);
    struct zmk_keycode_state_changed ev = {.usage_page = 7, .keycode = 0x1e, .state = true, .timestamp = 123, .implicit_modifiers = 2};
    assert(__wrap_raise_zmk_keycode_state_changed(ev) == 17);
    assert(raised == 1 && seen[0].keycode == 0x3a && seen[0].timestamp == 123 && seen[0].implicit_modifiers == 2);
    assert(gmk_flip_toggle() == 0);
    mac      = true;
    ev.state = false;
    assert(__wrap_raise_zmk_keycode_state_changed(ev) == 17);
    assert(seen[1].keycode == 0x3a && !seen[1].state);
    assert(gmk_flip_toggle() == 0);
    ev.state   = true;
    ev.keycode = 0x22;
    assert(__wrap_raise_zmk_keycode_state_changed(ev) == 0 && brightness == -1 && raised == 2);
    ev.state = false;
    assert(__wrap_raise_zmk_keycode_state_changed(ev) == 0 && brightness == -1 && raised == 2);
    ev.usage_page = 12;
    ev.keycode    = 0xea;
    ev.state      = true;
    assert(__wrap_raise_zmk_keycode_state_changed(ev) == 17 && seen[2].usage_page == 12);
}
int main(void) {
    start();
    assert(gmk_flip_toggle() == 0);
    radio_test_time = 250;
    gmk_flip_poll(true);
    usb_ready = false;
    run_work();
    assert(writes == 0 && persisted == 0);
    radio_test_time = 1250;
    usb_ready       = true;
    gmk_flip_poll(true);
    run_work();
    assert(writes == 1 && persisted == 1);
    persistence_is_deferred_coalesced_gated_and_retried();
    native_events_and_strict_settings_load();
    puts("Function flip adapter: native events, strict load, deferred/coalesced saves, retry and in-flight changes passed");
}

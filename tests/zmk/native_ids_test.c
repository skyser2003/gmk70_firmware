/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <drivers/behavior.h>
#include <zephyr/settings/settings.h>
#include "../../firmware/zmk/studio/settings_ids.h"
#include "settings_name_test.h"

static const struct device       devices[3]                          = {{.name = "a", .ready = true}, {.name = "b", .ready = true}, {.name = "c", .ready = true}};
struct zmk_behavior_ref          native_zmk_behavior_ref[3]          = {{.device = &devices[0]}, {.device = &devices[1]}, {.device = &devices[2]}};
struct zmk_behavior_local_id_map native_zmk_behavior_local_id_map[3] = {{.device = &devices[0]}, {.device = &devices[1]}, {.device = &devices[2]}};
static unsigned                  invokes, saves;
unsigned                         native_ids_irq_depth;
static char                      saved_name[32], saved_value[32];
const struct device             *z_impl_behavior_get_binding(const char *name);
const struct device             *behavior_get_binding(const char *name) {
    return z_impl_behavior_get_binding(name);
}
int behavior_keymap_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    (void)binding;
    (void)event;
    ++invokes;
    return 0;
}
int behavior_keymap_binding_released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    (void)binding;
    (void)event;
    return 0;
}
int behavior_keymap_binding_convert_central_state_dependent_params(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    (void)binding;
    (void)event;
    return 0;
}
int behavior_get_locality(const struct device *device, enum behavior_locality *locality) {
    (void)device;
    *locality = BEHAVIOR_LOCALITY_CENTRAL;
    return 0;
}
int settings_name_steq(const char *name, const char *key, const char **next) {
    return gmk_test_settings_name_steq(name, key, next);
}
int settings_save_one(const char *name, const void *value, size_t size) {
    assert(native_ids_irq_depth == 0);
    assert(strlen(name) < sizeof(saved_name) && size < sizeof(saved_value));
    strcpy(saved_name, name);
    memcpy(saved_value, value, size);
    saved_value[size] = '\0';
    ++saves;
    return 0;
}
int settings_call_set_handler(const char *name, size_t size, settings_read_cb read, void *context, const struct settings_load_arg *arg) {
    assert(!arg && native_ids_irq_depth == 1);
    assert(!strncmp(name, "behavior/", 9));
    int result = native_behavior_settings.set(name + 9, size, read, context);
    /* The pinned generic native dispatcher intentionally ignores h_set errors. */
    (void)result;
    return 0;
}
static ssize_t read_name(void *context, void *value, size_t size) {
    memcpy(value, context, size);
    return (ssize_t)size;
}
static int load(const char *id, const char *name) {
    return native_behavior_settings.set(id, strlen(name), read_name, (void *)name);
}
static int observe(const char *name, size_t size, settings_read_cb read, void *context, void *param) {
    (void)name;
    (void)size;
    (void)read;
    (void)context;
    (void)param;
    assert(!"reservation must not invoke a direct read callback");
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    bool                             reserved  = !strcmp(argv[1], "reserved") || !strcmp(argv[1], "behavior-subtree");
    bool                             exhausted = !strcmp(argv[1], "headroom");
    struct gmk_settings_read_context record    = {.value = (const uint8_t *)"retired", .size = 7};
    if (!strcmp(argv[1], "zero-seed")) {
        assert(!gmk_settings_reserve_orphan_id("behavior/local_id/3", &record, NULL));
        assert(zmk_behavior_get_local_id("a") == 0 && saves == 0);
        assert(!native_behavior_settings.commit());
        assert(zmk_behavior_get_local_id("a") == 4 && zmk_behavior_get_local_id("b") == 5 && zmk_behavior_get_local_id("c") == 6);
        assert(saves == 3 && zmk_behavior_find_behavior_name_from_local_id(3) == NULL);
        puts("Pinned native behavior IDs: zero-ID seed restored before native allocation");
        return 0;
    }
    assert(!load("local_id/1", "a"));
    if (exhausted) {
        assert(load("local_id/65534", "retired") == -EINVAL);
        assert(gmk_settings_reserve_orphan_id("behavior/local_id/65534", &record, NULL) == -EOVERFLOW);
        assert(saves == 0 && zmk_behavior_get_local_id("a") == 1 && zmk_behavior_get_local_id("b") == 0 && zmk_behavior_get_local_id("c") == 0);
        assert(!native_behavior_settings.commit());
        assert(zmk_behavior_get_local_id("b") == 2 && zmk_behavior_get_local_id("c") == 3);
        assert(zmk_behavior_find_behavior_name_from_local_id(65534) == NULL);
        puts("Pinned native behavior IDs: exhausted orphan reservation rejected before wrap");
        return 0;
    }
    assert(!load("local_id/2", "b"));
    if (!strcmp(argv[1], "invalid")) {
        assert(gmk_settings_reserve_orphan_id("behavior/local_id/0", &record, NULL) == -EINVAL);
        assert(gmk_settings_reserve_orphan_id("behavior/local_id/65535", &record, NULL) == -EINVAL);
        assert(gmk_settings_reserve_orphan_id("behavior/local_id/3x", &record, NULL) == -EINVAL);
        assert(saves == 0 && native_ids_irq_depth == 0);
        puts("Pinned native behavior IDs: malformed/reserved IDs rejected");
        return 0;
    }
    assert(load("local_id/3", "retired") == -EINVAL);
    assert(zmk_behavior_find_behavior_name_from_local_id(3) == NULL);
    if (strcmp(argv[1], "baseline")) {
        struct settings_load_arg arg = {0};
        if (!strcmp(argv[1], "behavior-subtree")) arg.subtree = "behavior";
        if (!strcmp(argv[1], "keymap-subtree")) arg.subtree = "keymap";
        if (!strcmp(argv[1], "direct")) arg.cb = observe;
        assert(!gmk_settings_reserve_orphan_id("behavior/local_id/3", &record, !strcmp(argv[1], "reserved") ? NULL : &arg));
    }
    assert(zmk_behavior_get_local_id("a") == 1 && zmk_behavior_get_local_id("b") == 2);
    assert(!native_behavior_settings.commit());
    assert(saves == 1 && !strcmp(saved_value, "c"));
    assert(zmk_behavior_get_local_id("c") == (reserved ? 4 : 3));
    const char                 *orphan  = zmk_behavior_find_behavior_name_from_local_id(3);
    struct zmk_behavior_binding binding = {.local_id = 3, .behavior_dev = orphan};
    int                         result  = zmk_behavior_invoke_binding(&binding, (struct zmk_behavior_binding_event){0}, true);
    if (reserved) {
        assert(orphan == NULL && result == ZMK_BEHAVIOR_TRANSPARENT && invokes == 0);
        assert(!strcmp(saved_name, "behavior/local_id/4"));
    } else {
        assert(orphan && !strcmp(orphan, "c") && result == 0 && invokes == 1);
        assert(!strcmp(saved_name, "behavior/local_id/3"));
    }
    printf("Pinned native behavior IDs: %s, A/B stable, new C=%u, old binding result=%d\n", reserved ? "orphan high-water reserved" : "orphan ID reused", zmk_behavior_get_local_id("c"), result);
    return 0;
}

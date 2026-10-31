/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/zmk/studio/bool_setting.h"

uint32_t radio_test_time;
unsigned radio_test_locked;

static struct gmk_bool_setting  first, second;
static struct gmk_bool_setting *change_during_save;
static bool                     value_during_save;
static unsigned                 writes;
static int                      save_result;
static uint8_t                  first_saved, second_saved;

int gmk_zmk_settings_save_runtime(const char *name, const void *value, size_t size) {
    assert(!radio_test_locked && size == 1);
    assert(strcmp(name, "first/state") == 0 || strcmp(name, "second/state") == 0);
    ++writes;
    if (change_during_save) {
        assert(gmk_bool_setting_set(change_during_save, value_during_save));
        change_during_save = NULL;
    }
    if (!save_result) {
        uint8_t *saved = strcmp(name, "first/state") == 0 ? &first_saved : &second_saved;
        *saved         = *(const uint8_t *)value;
    }
    return save_result;
}

static void start(bool default_value) {
    radio_test_time    = 0;
    writes             = 0;
    save_result        = 0;
    first_saved        = 0;
    second_saved       = 0;
    change_during_save = NULL;
    gmk_bool_setting_init(&first, "first/state", default_value);
    gmk_bool_setting_init(&second, "second/state", !default_value);
}

static void run_work(struct gmk_bool_setting *setting) {
    assert(setting->work.pending && !radio_test_locked);
    setting->work.pending = false;
    setting->work.handler(&setting->work);
}

static void defaults_are_clean_and_instances_are_independent(void) {
    start(true);
    assert(gmk_bool_setting_get(&first));
    assert(!gmk_bool_setting_get(&second));
    radio_test_time = 1000;
    gmk_bool_setting_poll(&first, true);
    gmk_bool_setting_poll(&second, true);
    assert(!first.work.pending && !second.work.pending && writes == 0);
    assert(!gmk_bool_setting_toggle(&first));
    assert(gmk_bool_setting_toggle(&second));
    radio_test_time = 1250;
    gmk_bool_setting_poll(&first, true);
    gmk_bool_setting_poll(&second, true);
    assert(writes == 0);
    run_work(&second);
    run_work(&first);
    assert(writes == 2 && first_saved == 0 && second_saved == 1);
}

static void debounce_is_gated_and_unchanged_sets_do_not_delay_it(void) {
    start(false);
    assert(gmk_bool_setting_set(&first, true));
    radio_test_time = 249;
    assert(!gmk_bool_setting_set(&first, true));
    gmk_bool_setting_poll(&first, true);
    assert(!first.work.pending && writes == 0);
    radio_test_time = 250;
    gmk_bool_setting_poll(&first, false);
    assert(!first.work.pending && writes == 0);
    gmk_bool_setting_poll(&first, true);
    gmk_bool_setting_poll(&first, true);
    assert(first.work.pending && writes == 0);
    run_work(&first);
    assert(writes == 1 && first_saved == 1);
    radio_test_time = 5000;
    gmk_bool_setting_poll(&first, true);
    assert(!first.work.pending);
}

static void failed_writes_retry_after_one_second(void) {
    start(false);
    assert(gmk_bool_setting_set(&first, true));
    radio_test_time = 250;
    gmk_bool_setting_poll(&first, true);
    save_result = -EBUSY;
    run_work(&first);
    assert(writes == 1 && first_saved == 0);
    radio_test_time = 1249;
    gmk_bool_setting_poll(&first, true);
    assert(!first.work.pending);
    radio_test_time = 1250;
    gmk_bool_setting_poll(&first, true);
    save_result = 0;
    run_work(&first);
    assert(writes == 2 && first_saved == 1);
}

static void changes_coalesce_and_in_flight_changes_remain_dirty(void) {
    start(false);
    assert(gmk_bool_setting_toggle(&first));
    assert(!gmk_bool_setting_toggle(&first));
    radio_test_time = 500;
    gmk_bool_setting_poll(&first, true);
    assert(!first.work.pending);
    assert(gmk_bool_setting_toggle(&first));
    radio_test_time = 750;
    gmk_bool_setting_poll(&first, true);
    change_during_save = &first;
    value_during_save  = false;
    run_work(&first);
    assert(writes == 1 && first_saved == 1 && !gmk_bool_setting_get(&first));
    radio_test_time = 1750;
    gmk_bool_setting_poll(&first, true);
    run_work(&first);
    assert(writes == 2 && first_saved == 0);
}

static void pending_work_saves_the_latest_value(void) {
    start(false);
    assert(gmk_bool_setting_toggle(&first));
    radio_test_time = 250;
    gmk_bool_setting_poll(&first, true);
    assert(!gmk_bool_setting_toggle(&first));
    run_work(&first);
    assert(writes == 1 && first_saved == 0);
    radio_test_time = 2000;
    gmk_bool_setting_poll(&first, true);
    assert(!first.work.pending);
}

static ssize_t read_byte(void *ctx, void *out, size_t size) {
    assert(size == 1);
    memcpy(out, ctx, 1);
    return 1;
}

static ssize_t read_failure(void *ctx, void *out, size_t size) {
    (void)out;
    (void)size;
    return *(const ssize_t *)ctx;
}

static void loads_require_one_boolean_byte_and_start_clean(void) {
    start(true);
    uint8_t value = 0;
    assert(gmk_bool_setting_load(&first, 0, read_byte, &value) == -EINVAL);
    assert(gmk_bool_setting_load(&first, 2, read_byte, &value) == -EINVAL);
    ssize_t read_result = -EIO;
    assert(gmk_bool_setting_load(&first, 1, read_failure, &read_result) == -EIO);
    read_result = 0;
    assert(gmk_bool_setting_load(&first, 1, read_failure, &read_result) == -EINVAL);
    value = 2;
    assert(gmk_bool_setting_load(&first, 1, read_byte, &value) == -EINVAL);
    assert(gmk_bool_setting_get(&first));
    value = 0;
    assert(gmk_bool_setting_load(&first, 1, read_byte, &value) == 0);
    assert(!gmk_bool_setting_get(&first));
    radio_test_time = 5000;
    gmk_bool_setting_poll(&first, true);
    assert(!first.work.pending && writes == 0);
    value = 1;
    assert(gmk_bool_setting_load(&first, 1, read_byte, &value) == 0);
    assert(gmk_bool_setting_get(&first));
    gmk_bool_setting_poll(&first, true);
    assert(!first.work.pending && writes == 0);
}

static void debounce_and_retry_survive_uptime_wrap(void) {
    start(false);
    radio_test_time = UINT32_MAX - 100;
    assert(gmk_bool_setting_toggle(&first));
    radio_test_time = 148;
    gmk_bool_setting_poll(&first, true);
    assert(!first.work.pending);
    radio_test_time = 149;
    gmk_bool_setting_poll(&first, true);
    save_result = -EBUSY;
    run_work(&first);
    radio_test_time = 1148;
    gmk_bool_setting_poll(&first, true);
    assert(!first.work.pending);
    radio_test_time = 1149;
    gmk_bool_setting_poll(&first, true);
    save_result = 0;
    run_work(&first);
    assert(writes == 2 && first_saved == 1);

    start(false);
    radio_test_time = UINT32_MAX - 500;
    assert(gmk_bool_setting_toggle(&first));
    radio_test_time = UINT32_MAX - 250;
    gmk_bool_setting_poll(&first, true);
    save_result = -EBUSY;
    run_work(&first);
    radio_test_time = 748;
    gmk_bool_setting_poll(&first, true);
    assert(!first.work.pending);
    radio_test_time = 749;
    gmk_bool_setting_poll(&first, true);
    save_result = 0;
    run_work(&first);
    assert(writes == 2 && first_saved == 1);
}

int main(void) {
    defaults_are_clean_and_instances_are_independent();
    debounce_is_gated_and_unchanged_sets_do_not_delay_it();
    failed_writes_retry_after_one_second();
    changes_coalesce_and_in_flight_changes_remain_dirty();
    pending_work_saves_the_latest_value();
    loads_require_one_boolean_byte_and_start_clean();
    debounce_and_retry_survive_uptime_wrap();
    puts("Boolean settings: defaults, strict load, independent state, gated debounce, retry, coalescing and in-flight changes passed");
}

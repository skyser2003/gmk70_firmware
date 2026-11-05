/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zmk/rgb_underglow.h>
#include <zmk/studio/core.h>
#include "../../firmware/zmk/studio/settings_backend.h"

#include "../../firmware/zmk/lighting/rgb_settings.h"
int __wrap_settings_save_one(const char *name, const void *value, size_t size);
extern int (*rgb_test_init)(void);
extern int (*rgb_test_load)(const char *, size_t, settings_read_cb, void *);
int                             settings_backend_init(void);
uint32_t                        rgb_test_time;
const struct device             rgb_test_device = {0};
static struct settings_store   *backend;
static struct k_work_delayable *native_save;
static struct k_work           *retry_work;
static atomic_bool              stop, storage_busy, usb_ready, wired, writes_ready;
static atomic_uint              writes;
static atomic_bool              fail_write, replace_during_write;
static unsigned                 generic_saves;
static uint8_t                  saved[10];
static bool                     loaded;

uint32_t k_uptime_get_32(void) {
    return rgb_test_time;
}
int k_work_submit(struct k_work *work) {
    assert(!retry_work || retry_work == work);
    retry_work    = work;
    work->pending = true;
    return 1;
}
int k_work_submit_to_queue(void *queue, struct k_work *work) {
    (void)queue;
    (void)work;
    return 1;
}
void *zmk_workqueue_lowprio_work_q(void) {
    return NULL;
}
void k_work_init_delayable(struct k_work_delayable *work, void (*handler)(struct k_work *)) {
    work->work.handler = handler;
    native_save        = work;
}
int k_work_reschedule(struct k_work_delayable *work, uint32_t delay) {
    work->due          = rgb_test_time + delay;
    work->work.pending = true;
    return 1;
}
void k_timer_start(struct k_timer *timer, int initial, int period) {
    (void)timer;
    (void)initial;
    (void)period;
}
void k_timer_stop(struct k_timer *timer) {
    (void)timer;
}
int led_strip_update_rgb(const struct device *device, struct led_rgb *pixels, size_t count) {
    (void)device;
    (void)pixels;
    (void)count;
    return 0;
}
bool gmk_wired_is_left(void) {
    return true;
}
bool gmk_zmk_receiver_storage_claim(void) {
    return !atomic_exchange(&storage_busy, true);
}
void gmk_zmk_receiver_storage_release(void) {
    assert(atomic_exchange(&storage_busy, false));
}
bool gmk_zmk_settings_flash_page(uint32_t address, const uint8_t data[256]) {
    atomic_fetch_add(&writes, 1);
    if (atomic_load(&fail_write)) return false;
    memcpy((void *)(uintptr_t)address, data, 256);
    if (atomic_exchange(&replace_during_write, false)) {
        assert(!zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 240, .s = 80, .b = 70}));
        native_save->work.pending = false;
        native_save->work.handler(&native_save->work);
    }
    return true;
}
enum zmk_studio_core_lock_state __real_zmk_studio_core_get_lock_state(void) {
    return ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED;
}
void settings_src_register(struct settings_store *store) {
    backend = store;
}
void settings_dst_register(struct settings_store *store) {
    assert(backend == store);
}
int settings_save_one(const char *name, const void *value, size_t size) {
    ++generic_saves;
    return backend->cs_itf->csi_save(backend, name, value, size);
}
int settings_name_steq(const char *name, const char *key, const char **next) {
    size_t len = strlen(key);
    if (strncmp(name, key, len) || (name[len] && name[len] != '/')) return 0;
    *next = name[len] ? name + len + 1 : NULL;
    return 1;
}
int settings_call_set_handler(const char *name, size_t size, settings_read_cb read, void *ctx, const struct settings_load_arg *arg) {
    (void)arg;
    if (strcmp(name, "rgb/underglow/state")) return 0;
    assert(size == sizeof(saved));
    assert(read(ctx, saved, sizeof(saved)) == (ssize_t)sizeof(saved));
    loaded = true;
    return rgb_test_load("state", size, read, ctx);
}
static void *storage_worker(void *arg) {
    (void)arg;
    while (!atomic_load(&stop)) {
        gmk_zmk_update_env_t env = {.left = true, .wired = atomic_load(&wired), .usb_configured = atomic_load(&usb_ready), .writes_ready = atomic_load(&writes_ready)};
        gmk_zmk_settings_poll(&env);
        usleep(100);
    }
    return NULL;
}
static void poll(void) {
    gmk_rgb_settings_poll(atomic_load(&wired) && atomic_load(&usb_ready) && atomic_load(&writes_ready));
}
static void system_work(void) {
    if (native_save->work.pending && rgb_test_time >= native_save->due) {
        native_save->work.pending = false;
        native_save->work.handler(&native_save->work);
    }
    if (retry_work && retry_work->pending) {
        retry_work->pending = false;
        retry_work->handler(retry_work);
    }
}
static void reload(uint16_t hue, uint8_t brightness) {
    loaded = false;
    assert(!backend->cs_itf->csi_load(backend, NULL) && loaded);
    assert(saved[0] == (uint8_t)hue && saved[1] == (uint8_t)(hue >> 8));
    assert(saved[2] == 80 && saved[3] == brightness);
    assert(saved[4] == 1 && saved[5] == 0 && saved[6] == 0 && saved[7] == 0 && saved[8] == 1 && saved[9] == 0);
    struct zmk_led_hsb color = zmk_rgb_underglow_calc_hue(0);
    assert(color.h == hue && color.s == 80 && color.b == brightness);
}
int main(int argc, char **argv) {
    alarm(10); /* Any parked synchronous save fails instead of hanging the suite. */
    void *flash = mmap((void *)0x0801c000, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(flash == (void *)0x0801c000);
    memset(flash, 0xff, 16384);
    assert(!settings_backend_init());
    assert(!rgb_test_init());
    assert(!gmk_rgb_settings_dirty());
    atomic_store(&writes_ready, true);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, storage_worker, NULL));
    assert(!zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 130, .s = 80, .b = 30}));
    assert(native_save->work.pending && native_save->due == 60000);
    if (argc > 1 && !strcmp(argv[1], "usb-gate")) atomic_store(&wired, true);
    rgb_test_time = 60000;
    system_work();
    assert(atomic_load(&writes) == 0); /* Wired but unconfigured must not use generic startup writes. */
    assert(gmk_rgb_settings_dirty());
    rgb_test_time = 180000;
    poll();
    system_work();
    assert(atomic_load(&writes) == 0);
    atomic_store(&wired, true);
    atomic_store(&usb_ready, true);
    poll();
    system_work();
    assert(atomic_load(&writes) > 0); /* Failed wireless native save must survive beyond its debounce. */
    assert(generic_saves == 0);
    assert(!gmk_rgb_settings_dirty());
    reload(130, 30);
    unsigned before = atomic_load(&writes);
    assert(!zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 200, .s = 80, .b = 40}));
    rgb_test_time += 60000;
    system_work();
    poll();
    atomic_store(&usb_ready, false); /* Loss after queueing must complete with an error, not park work. */
    system_work();
    assert(atomic_load(&writes) == before);
    atomic_store(&usb_ready, true);
    poll();
    system_work();
    assert(atomic_load(&writes) == before); /* Bound retries to one attempt per second. */
    rgb_test_time += 1000;
    atomic_store(&storage_busy, true);
    poll();
    system_work();
    assert(atomic_load(&writes) == before);
    atomic_store(&storage_busy, false);
    rgb_test_time += 1000;
    poll();
    system_work();
    assert(atomic_load(&writes) > before);
    reload(200, 40);
    /* Several native callbacks while ineligible coalesce to the last native bytes. */
    atomic_store(&wired, false);
    assert(!zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 210, .s = 80, .b = 50}));
    rgb_test_time += 60000;
    system_work();
    assert(!zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 220, .s = 80, .b = 60}));
    rgb_test_time += 60000;
    system_work();
    atomic_store(&wired, true);
    poll();
    system_work();
    reload(220, 60);
    /* An in-flight acknowledgment must not erase a newer native snapshot. */
    before = atomic_load(&writes);
    assert(!zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 230, .s = 80, .b = 65}));
    rgb_test_time += 60000;
    system_work();
    atomic_store(&replace_during_write, true);
    poll();
    system_work();
    assert(atomic_load(&writes) > before);
    reload(230, 65);
    assert(gmk_rgb_settings_dirty());
    rgb_test_time += 1000;
    poll();
    system_work();
    reload(240, 70);
    assert(!gmk_rgb_settings_dirty());
    /* A physical backend failure retains pending bytes while storage is unavailable. */
    assert(!zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 250, .s = 80, .b = 75}));
    rgb_test_time += 60000;
    system_work();
    atomic_store(&fail_write, true);
    poll();
    system_work();
    assert(!gmk_zmk_settings_ready());
    before = atomic_load(&writes);
    rgb_test_time += 1000;
    poll();
    system_work();
    assert(atomic_load(&writes) == before);
    atomic_store(&fail_write, false);
    assert(!settings_backend_init() && gmk_zmk_settings_ready());
    rgb_test_time += 1000;
    poll();
    system_work();
    reload(250, 75);
    /* Native behavior-ID startup policy is intentionally independent of USB. */
    atomic_store(&usb_ready, false);
    assert(!__wrap_settings_save_one("behavior/local_id/1", "kp", 2));
    assert(generic_saves == 1);
    assert(!rgb_test_init());
    assert(!settings_backend_init());
    reload(250, 75);
    atomic_store(&stop, true);
    pthread_join(thread, NULL);
    assert(!munmap(flash, 16384));
    puts("RGB native settings: deferred wireless save, USB dispatch gate, retry/coalescing, native reload and startup policy passed");
}

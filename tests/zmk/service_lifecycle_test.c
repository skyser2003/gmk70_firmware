/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "receiver.h"
#include "wired.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/class/usb_hid.h>

/* Narrow model of pinned kernel/init.c: APPLICATION initializers run first,
 * then static thread setup sets sleeping and schedules finite-delay threads.
 * kernel/sched.c wakeup has no effect before the sleeping bit exists. */
struct lifecycle_thread {
    void (*entry)(void *, void *, void *);
    int  delay;
    bool sleeping, ready, terminated;
};
#undef K_THREAD_DEFINE
#define K_THREAD_DEFINE(name, stack, fn, a, b, c, prio, opts, delay_ms)                                  \
    static struct lifecycle_thread        lifecycle_##name           = {.entry = fn, .delay = delay_ms}; \
    static struct lifecycle_thread *const name __attribute__((used)) = &lifecycle_##name
static jmp_buf               quantum;
static unsigned              feeds, usb_writes, flash_writes, stats_reads, now;
unsigned                     receiver_locked;
static bool                  missing_device;
static int                   hid_init_result;
static struct device         device;
static const struct hid_ops *callbacks;
static void (*wire_handler)(const uint8_t[32], uint8_t[32]);
static uint8_t incoming[32], sent[32], flash[0x40000];

static void before_service_ready(void) {
    assert(!feeds && !usb_writes && !flash_writes && !stats_reads);
}
const struct device *device_get_binding(const char *name) {
    assert(strcmp(name, "HID_1") == 0);
    before_service_ready();
    return missing_device ? NULL : &device;
}
void usb_hid_register_device(const struct device *dev, const uint8_t *desc, size_t size, const struct hid_ops *ops) {
    assert(dev == &device && size > 20 && desc[0] == 0x06);
    before_service_ready();
    callbacks = ops;
}
int usb_hid_init(const struct device *dev) {
    assert(dev == &device);
    before_service_ready();
    return hid_init_result;
}
int hid_int_ep_read(const struct device *dev, uint8_t *data, uint32_t size, uint32_t *length) {
    assert(dev == &device && size == 32);
    memcpy(data, incoming, 32);
    *length = 32;
    return 0;
}
int hid_int_ep_write(const struct device *dev, const uint8_t *data, uint32_t size, uint32_t *written) {
    assert(dev == &device && size == 32);
    (void)written;
    ++usb_writes;
    memcpy(sent, data, 32);
    return 0;
}
bool gmk_zmk_watchdog_init(void) {
    return true;
}
void gmk_zmk_watchdog_feed(void) {
    ++feeds;
}
bool gmk_wired_is_left(void) {
    return true;
}
bool gmk_wired_mode(void) {
    return true;
}
bool zmk_usb_is_hid_ready(void) {
    return true;
}
void gmk_wired_set_update_handler(void (*handler)(const uint8_t[32], uint8_t[32])) {
    before_service_ready();
    wire_handler = handler;
}
bool gmk_wired_try_get_stats(struct gmk_wired_stats *stats) {
    ++stats_reads;
    *stats = (struct gmk_wired_stats){0};
    return true;
}
bool gmk_wired_update(const uint8_t in[32], uint8_t out[32]) {
    (void)in;
    (void)out;
    assert(false);
    return false;
}
uint32_t k_uptime_get_32(void) {
    return now;
}
void k_thread_start(void *thread) {
    struct lifecycle_thread *t = thread;
    if (t->sleeping) {
        t->sleeping = false;
        t->ready    = true;
    }
}
void k_sleep(int ms) {
    assert(ms == 1);
    ++now;
    longjmp(quantum, 1);
}
void sys_reboot(int type) {
    (void)type;
    assert(false);
}
const uint8_t *gmk_flash_view(uint32_t address) {
    assert(address >= GMK_FLASH_BASE && address < GMK_FLASH_BASE + sizeof(flash));
    return flash + address - GMK_FLASH_BASE;
}
uint32_t gmk_running_size(void) {
    return 1024;
}
bool gmk_flash_page(uint32_t address, const uint8_t data[256]) {
    assert(address == GMK_COMMIT_BASE);
    memcpy(flash + address - GMK_FLASH_BASE, data, 256);
    ++flash_writes;
    return true;
}
#include "service.c"

static void initialize_static_thread(struct lifecycle_thread *t) {
    t->sleeping = true;
    t->ready    = false;
    if (t->delay != SYS_FOREVER_MS) k_thread_start(t);
}
static void run_ready_quantum(struct lifecycle_thread *t) {
    if (!t->ready || t->terminated) return;
    /* A quantum executes one worker iteration. Only startup scheduling is
     * modeled, not a full context switch implementation or kernel timing. */
    if (!setjmp(quantum)) {
        t->entry(NULL, NULL, NULL);
        t->terminated = true;
        t->ready      = false;
    }
}
static void queue_packet(unsigned op) {
    memset(incoming, 0, 32);
    incoming[0] = 0x7f;
    incoming[1] = 0x70;
    incoming[2] = 1;
    incoming[4] = op;
    if (op == GMK_BEGIN) {
        incoming[5] = 12;
        incoming[8] = 0x12;
        gmk_write_u32(incoming + 16, 1024);
        incoming[24] = 1;
    }
    assert(callbacks);
    callbacks->int_out_ready(&device);
}
int main(int argc, char **argv) {
    bool fail       = argc > 1;
    missing_device  = fail && strcmp(argv[1], "missing") == 0;
    hid_init_result = fail && !missing_device ? -EIO : 0;
    memset(flash, 0xa5, sizeof(flash));
    assert(watchdog_init() == 0);
    /* Production APPLICATION service registration occurs before static setup. */
    int result = service_init();
    if (fail) {
        assert(result == (missing_device ? -ENODEV : -EIO) && !wire_handler);
        if (!missing_device) queue_packet(GMK_BEGIN);
    } else {
        assert(result == 0 && wire_handler);
        queue_packet(GMK_INFO);
    }
    before_service_ready();
    run_ready_quantum(update_worker); /* No pre-initialization wakeup works. */
    before_service_ready();
    initialize_static_thread(update_worker);
    run_ready_quantum(update_worker);
    if (fail) {
        /* A failed HID init can already have registered callbacks. Even a
         * queued valid mutation must not be consumed or feed the watchdog. */
        before_service_ready();
        run_ready_quantum(update_worker);
        before_service_ready();
    } else {
        assert(usb_writes == 1 && sent[4] == GMK_INFO && sent[5] == GMK_BUSY);
        queue_packet(GMK_STATUS);
        run_ready_quantum(update_worker);
        assert(usb_writes == 2 && sent[4] == GMK_INFO && sent[5] == GMK_OK && sent[7] == 3);
        assert(gmk_read_u32(sent + 16) == 1024 && gmk_read_u32(sent + 24) == 1 && !flash_writes && stats_reads == 2);
#ifdef CONFIG_GMK70_STAGING_QUALIFICATION
        assert(feeds == 0);
#else
        assert(feeds == 2);
#endif
    }
    puts("ZMK APPLICATION/static-thread lifecycle and raw HID integration passed");
}

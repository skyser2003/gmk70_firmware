/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "radio.h"
#include "selection.h"
#include "../wired/wired.h"
#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
#    include <zmk/events/keycode_state_changed.h>
#endif

static struct gmk_radio_core     core;
static struct gmk_radio_selector selector;
static struct k_spinlock         lock;
static struct k_work             work;
static bool                      available;
static uint8_t                   pair_raw;
static uint32_t                  cancel_epoch;
static struct radio_request {
    uint32_t epoch;
    uint8_t  channel, raw;
    bool     pending, pair, clear;
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
    bool marker;
#endif
} mailbox;

static bool valid_mode(uint8_t raw) {
    return raw >= 1 && raw <= 3;
}
static int admission_locked(uint8_t raw) {
    if (!available) return -ENODEV;
    return valid_mode(raw) ? 0 : -EPERM;
}
static bool control_busy_locked(void) {
    return core.waiting || core.pending_select || core.pending_pair || core.inflight == GMK_RADIO_TX_SELECT || core.inflight == GMK_RADIO_TX_PAIR;
}
static void request_locked(uint8_t channel, bool pair, uint8_t raw) {
    mailbox.channel = channel;
    mailbox.raw     = raw;
    mailbox.pair    = pair;
    mailbox.pending = true;
    mailbox.epoch   = cancel_epoch;
}
static void cancel_pair_locked(void) {
    ++cancel_epoch;
    gmk_radio_selector_cancel(&selector);
    if (mailbox.pending && mailbox.pair) mailbox.pending = false;
    core.pending_pair = false;
}
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
static bool hid_idle(void) {
    const struct zmk_hid_keyboard_report_body *keyboard = &zmk_hid_get_keyboard_report()->body;
    const struct zmk_hid_consumer_report_body *consumer = &zmk_hid_get_consumer_report()->body;
    if (keyboard->modifiers || zmk_hid_get_explicit_mods()) return false;
    for (unsigned i = 0; i < sizeof(keyboard->keys) / sizeof(keyboard->keys[0]); ++i)
        if (keyboard->keys[i]) return false;
    for (unsigned i = 0; i < sizeof(consumer->keys) / sizeof(consumer->keys[0]); ++i)
        if (consumer->keys[i]) return false;
    return true;
}
static bool marker_ready_locked(void) {
    return available && core.selected && core.active == core.selected && core.connection == GMK_RADIO_CONNECTED && !core.count && !core.release && !core.waiting && !core.pending_select && !core.pending_pair && !core.inflight && !mailbox.pending;
}
static void emit_marker(void) {
    uint8_t          raw   = gmk_host_switch();
    k_spinlock_key_t key   = k_spin_lock(&lock);
    bool             ready = marker_ready_locked() && valid_mode(raw);
    k_spin_unlock(&lock, key);
    if (!ready || !hid_idle()) return;
    struct zmk_keycode_state_changed event   = {.usage_page = 7, .keycode = 0x73, .state = true, .timestamp = k_uptime_get()};
    int                              pressed = raise_zmk_keycode_state_changed(event);
    event.state                              = false;
    event.timestamp                          = k_uptime_get();
    int released                             = raise_zmk_keycode_state_changed(event);
    if (pressed < 0 || released < 0) gmk_radio_clear_hid();
}
int gmk_radio_test_marker(void) {
    if (!hid_idle()) return -EBUSY;
    uint8_t          raw    = gmk_host_switch();
    k_spinlock_key_t key    = k_spin_lock(&lock);
    int              result = -ENOTCONN;
    if (mailbox.marker)
        result = -EBUSY;
    else if (marker_ready_locked() && valid_mode(raw)) {
        mailbox.marker = true;
        result         = 0;
    }
    k_spin_unlock(&lock, key);
    if (!result) k_work_submit(&work);
    return result;
}
#endif
static void work_handler(struct k_work *item) {
    ARG_UNUSED(item);
    for (unsigned pass = 0; pass < 2; ++pass) {
        k_spinlock_key_t     key     = k_spin_lock(&lock);
        struct radio_request request = mailbox;
        mailbox.pending = mailbox.clear = false;
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
        mailbox.marker = false;
#endif
        k_spin_unlock(&lock, key);
        if (request.pending || request.clear) gmk_radio_clear_hid();
        uint8_t raw = gmk_host_switch();
        key         = k_spin_lock(&lock);
        if (request.pending && !mailbox.pending && available && valid_mode(raw) && (!request.pair || (raw == request.raw && request.epoch == cancel_epoch))) {
            gmk_radio_core_select(&core, request.channel, request.pair);
            if (request.pair) pair_raw = raw;
        }
        bool again = mailbox.pending || mailbox.clear;
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
        again = again || mailbox.marker;
#endif
        k_spin_unlock(&lock, key);
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
        if (request.marker && !request.pending && !request.clear) emit_marker();
#endif
        if (!again) return;
    }
    k_work_submit(&work);
}
int gmk_radio_select(uint8_t channel, bool pair) {
    if (channel > GMK_HOST_DONGLE || (pair && channel == GMK_HOST_USB)) return -EINVAL;
    uint8_t          raw    = gmk_host_switch();
    k_spinlock_key_t key    = k_spin_lock(&lock);
    int              result = admission_locked(raw);
    if (!result) {
        cancel_pair_locked();
        request_locked(channel, pair, raw);
    }
    k_spin_unlock(&lock, key);
    if (!result) k_work_submit(&work);
    return result;
}
int gmk_radio_hold(uint8_t channel, uint32_t position) {
    if (channel > GMK_HOST_DONGLE || position >= 72) return -EINVAL;
    uint8_t          raw    = gmk_host_switch();
    k_spinlock_key_t key    = k_spin_lock(&lock);
    int              result = admission_locked(raw);
    if (!result) {
        cancel_pair_locked();
        result = gmk_radio_selector_press(&selector, channel, position, k_uptime_get_32());
        request_locked(channel, false, raw);
    }
    k_spin_unlock(&lock, key);
    if (!result) k_work_submit(&work);
    return result;
}
void gmk_radio_unhold(uint32_t position) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (selector.held_channel && selector.held_position == position) cancel_pair_locked();
    gmk_radio_selector_release(&selector, position);
    k_spin_unlock(&lock, key);
}
void gmk_radio_poll(void) {
    uint8_t          raw = gmk_host_switch(), channel;
    uint32_t         now = k_uptime_get_32();
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (available) {
        if (!valid_mode(raw) || (selector.have_candidate && selector.candidate_raw != raw)) cancel_pair_locked();
        if (core.pending_pair && (raw != pair_raw || !valid_mode(raw))) core.pending_pair = false;
        if (gmk_radio_selector_switch(&selector, raw, now, core.last_bt, &channel)) {
            if (channel == UINT8_MAX) {
                cancel_pair_locked();
                mailbox.clear = true;
            } else
                request_locked(channel, false, raw);
        }
        if (gmk_radio_selector_pair(&selector, now, &channel) && valid_mode(raw)) request_locked(channel, true, raw);
        if (core.clear_requested) {
            core.clear_requested = false;
            mailbox.clear        = true;
        }
    }
    bool pending = mailbox.pending || mailbox.clear;
    k_spin_unlock(&lock, key);
    if (pending) k_work_submit(&work);
}
bool gmk_radio_selected(void) {
    k_spinlock_key_t key      = k_spin_lock(&lock);
    bool             selected = available && core.selected != GMK_HOST_USB;
    k_spin_unlock(&lock, key);
    return selected;
}
static int submit(bool consumer, const uint8_t report[8]) {
    uint8_t          raw    = gmk_host_switch();
    k_spinlock_key_t key    = k_spin_lock(&lock);
    int              result = admission_locked(raw);
    if (!result) result = gmk_radio_core_report(&core, consumer, report);
    k_spin_unlock(&lock, key);
    return result;
}
int gmk_radio_submit_keyboard(const uint8_t report[8]) {
    return submit(false, report);
}
int gmk_radio_submit_consumer(uint16_t usage) {
    const uint8_t report[8] = {(uint8_t)usage, (uint8_t)(usage >> 8)};
    return submit(true, report);
}
void gmk_radio_release(bool consumer) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (available) gmk_radio_core_release(&core, consumer);
    k_spin_unlock(&lock, key);
}
void gmk_radio_notification(uint8_t command, uint8_t value) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (available) gmk_radio_core_notification(&core, command, value);
    k_spin_unlock(&lock, key);
}
void gmk_radio_ack(void) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (available) gmk_radio_core_ack(&core);
    k_spin_unlock(&lock, key);
}
bool gmk_radio_next(uint32_t now, struct gmk_radio_frame *frame) {
    k_spinlock_key_t key    = k_spin_lock(&lock);
    bool             result = available && gmk_radio_core_next(&core, now, frame);
    k_spin_unlock(&lock, key);
    return result;
}
void gmk_radio_sent(uint32_t now, bool success) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    if (available) gmk_radio_core_sent(&core, now, success);
    k_spin_unlock(&lock, key);
}
bool gmk_radio_control_busy(void) {
    k_spinlock_key_t key  = k_spin_lock(&lock);
    bool             busy = available && control_busy_locked();
    k_spin_unlock(&lock, key);
    return busy;
}
void gmk_radio_get(struct gmk_radio_status *out) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    *out                 = (struct gmk_radio_status){.errors = core.errors, .selections = core.selections, .reports = core.reports, .selected = core.selected, .active = core.active, .connection = core.connection, .queued = core.count, .waiting = available && (control_busy_locked() || mailbox.pending), .available = available};
    k_spin_unlock(&lock, key);
}
int __real_zmk_keymap_set_layer_binding_at_idx(zmk_keymap_layer_id_t layer, uint16_t binding_idx, const struct zmk_behavior_binding binding);
int __wrap_zmk_keymap_set_layer_binding_at_idx(zmk_keymap_layer_id_t layer, uint16_t binding_idx, const struct zmk_behavior_binding binding) {
    int              result = __real_zmk_keymap_set_layer_binding_at_idx(layer, binding_idx, binding);
    k_spinlock_key_t key    = k_spin_lock(&lock);
    cancel_pair_locked();
    k_spin_unlock(&lock, key);
    return result;
}
static int radio_init(void) {
    gmk_radio_core_init(&core);
    selector     = (struct gmk_radio_selector){0};
    mailbox      = (struct radio_request){0};
    pair_raw     = 0;
    cancel_epoch = 0;
    available    = gmk_wired_is_left();
    k_work_init(&work, work_handler);
    return 0;
}
SYS_INIT(radio_init, POST_KERNEL, 87);

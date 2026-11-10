/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../firmware/zmk/power/power.h"
#include "../../firmware/zmk/power/protocol.h"
#include "../../firmware/zmk/battery/battery.h"
#include "../../firmware/zmk/wired/wired.h"
#include "../../firmware/zmk/lighting/rgb_settings.h"
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/pm.h>
#include <zmk/usb.h>
#include <zephyr/input/input.h>
#ifdef TEST_NATIVE_RGB
#    include <zmk/rgb_underglow.h>
#endif

static struct k_thread         service_thread = {1}, activity_thread = {2};
k_tid_t                        test_power_thread = &service_thread;
uint32_t                       test_power_now, test_primask, test_basepri;
static bool                    left_role = true, cable, receiver_free = true, lease, dirty, radio_idle_state = true, battery_idle_state = true, wired_idle_state = true, wake_pending, paused, critical_prepare, input_during_claim;
static bool                    peer_traffic;
static unsigned                claims, releases, wake_prepares, radio_quiesces, stops, resets, peer_pulses, report_clears;
static jmp_buf                 reset_jump;
static bool                    usb_sleep, radio_paused;
static enum usb_dc_status_code usb_status       = USB_DC_UNKNOWN;
static enum zmk_usb_conn_state native_usb_state = ZMK_USB_CONN_NONE;
static uint32_t                systick = 7, scr = 0x10;
static unsigned                shallow_stops;
#ifdef CONFIG_GMK70_EVENT_IDLE
static unsigned scheduling_notifications;
void            gmk_service_notify(void) {
    ++scheduling_notifications;
}
void gmk_scan_notify(void) {}
#endif
static uint32_t exti_pending;
static uint32_t kernel_pending;
static bool     rx_wakes;
static bool     split_routing;
struct mmio_write {
    uintptr_t address;
    uint32_t  value;
};
static struct mmio_write         mmio_writes[32];
static unsigned                  mmio_write_count;
static struct gmk_battery_status battery = {.valid = true, .percentage = 80};
int                              set_state(enum zmk_activity_state state);
void                             activity_work_handler(struct k_work *work);
bool                             is_usb_power_present(void);

int raise_zmk_activity_state_changed(struct zmk_activity_state_changed event) {
    return power_test_listener->callback(&(zmk_event_t){.activity = event});
}
static void activity(enum zmk_activity_state state) {
    (void)set_state(state);
}
bool gmk_wired_is_left(void) {
    return left_role;
}
bool gmk_wired_mode(void) {
    return true;
}
bool gmk_split_available(void) {
    return split_routing;
}
bool gmk_wired_power_idle(void) {
    return wired_idle_state;
}
bool gmk_wired_power_input_idle(void) {
    return wired_idle_state || peer_traffic;
}
bool gmk_wired_power_prepare(bool critical) {
    critical_prepare = critical;
    ++wake_prepares;
    return true;
}
void gmk_wired_power_pause(bool value) {
    paused = value;
}
void gmk_wired_power_resume(void) {
    assert(!radio_paused); /* Report admission precedes the waking key. */
    paused = false;
}
bool gmk_wired_power_wake_pending(void) {
    return wake_pending;
}
bool gmk_wired_try_get_stats(struct gmk_wired_stats *stats) {
    *stats = (struct gmk_wired_stats){0};
    return true;
}
bool gmk_wired_split_command(const uint8_t request[32], uint8_t response[32]) {
    (void)request;
    (void)response;
    assert(false);
    return false;
}
bool gmk_zmk_service_idle(void) {
    return receiver_free && !lease;
}
bool gmk_zmk_storage_claim(void) {
    ++claims;
    if (!receiver_free || lease) return false;
    lease = true;
    if (input_during_claim) power_test_pointing(&(struct input_event){1}, NULL);
    return true;
}
void gmk_zmk_storage_release(void) {
    assert(lease);
    lease = false;
    ++releases;
}
bool gmk_controls_dirty(void) {
    return dirty;
}
bool gmk_flip_dirty(void) {
    return false;
}
bool gmk_nkro_dirty(void) {
    return false;
}
#ifndef TEST_NATIVE_RGB
bool gmk_rgb_settings_dirty(void) {
    return false;
}
#endif
uint32_t gmk_controls_sleep_timeout_ms(void) {
    return 60000;
}
bool gmk_controls_usb_sleep(void) {
    return usb_sleep;
}
enum zmk_usb_conn_state zmk_usb_get_conn_state(void) {
    return native_usb_state;
}
enum zmk_usb_conn_state __wrap_zmk_usb_get_conn_state(void);
enum usb_dc_status_code zmk_usb_get_status(void) {
    return usb_status;
}
bool gmk_lighting_power_quiesce(bool enabled) {
    (void)enabled;
    return true;
}
bool gmk_radio_idle(void) {
    return radio_idle_state;
}
bool gmk_radio_power_quiesce(bool enabled) {
    radio_paused = enabled;
    if (enabled) ++radio_quiesces;
    return radio_idle_state;
}
bool gmk_battery_idle(void) {
    return battery_idle_state;
}
bool gmk_battery_power_quiesce(bool enabled) {
    (void)enabled;
    return battery_idle_state;
}
void gmk_battery_get(struct gmk_battery_status *out) {
    *out = battery;
}
void zmk_endpoint_clear_reports(void) {
    ++report_clears;
}
uint32_t sys_read32(uintptr_t address) {
    if (address == 0x40000010U) return cable ? 1U << 7 : 0;
    if (address == 0xe000e010U) return systick;
    if (address == 0xe000ed10U) return scr;
    if (address == 0x40001814U) return exti_pending;
    if (address == 0xe000ed04U) return kernel_pending;
    return 0;
}
void sys_write32(uint32_t value, uintptr_t address) {
    assert(address < 0x40014000U || address >= 0x40015000U); /* Never alter USB core state. */
    assert(mmio_write_count < 32);
    mmio_writes[mmio_write_count++] = (struct mmio_write){address, value};
    if (address == 0x40000018U && value == (1U << 25)) ++peer_pulses;
    if (address == 0xe000e010U) systick = value;
    if (address == 0xe000ed10U) scr = value;
}
void test_power_wfi(void) {
    assert(test_primask == 1 && test_basepri == 0 && !(scr & 4U) && !(systick & 1U));
    assert(cable && usb_sleep && paused && lease);
    ++shallow_stops;
    wake_pending = true;
    usb_status   = USB_DC_RESUME;
}
void k_busy_wait(uint32_t usec) {
    assert(usec == 100);
}
void wb32_stop_hardware(void) {
    assert(test_primask == 1 && test_basepri == 0 && lease && paused);
    ++stops;
    exti_pending = rx_wakes ? 1U << 10 : 1U << 13;
}
_Noreturn void sys_reboot(int type) {
    assert(type == 0);
    ++resets;
    longjmp(reset_jump, 1);
}
_Noreturn void sys_poweroff(void) {
    z_sys_poweroff();
}

bool     test_native_pm_suspended[2];
unsigned test_pm_suspend_calls, test_pm_resume_calls;
bool     test_pm_fail, test_pm_inject_activity;

static void test_native_pm_denies_early_or_usb_sleep(void) {
    assert(zmk_pm_suspend_devices() == -EBUSY);
    zmk_pm_resume_devices();
    cable = true;
    assert(is_usb_power_present()); /* Native inline getter must honor physical A7. */
    activity(ZMK_ACTIVITY_IDLE);
    test_power_now = 90000;
    gmk_power_poll(true);
    assert(!gmk_power_sleep_pending() && claims == 0);
    assert(zmk_pm_suspend_devices() == -EBUSY);
    cable = false;
}
static void test_physical_usb_preserves_native_hid_state(void) {
    cable            = true;
    native_usb_state = ZMK_USB_CONN_HID;
    usb_status       = USB_DC_SUSPEND;
    assert(__wrap_zmk_usb_get_conn_state() == ZMK_USB_CONN_HID);
    assert(is_usb_power_present());
    activity(ZMK_ACTIVITY_IDLE);
    test_power_now = 90000;
    gmk_power_poll(true);
    assert(!gmk_power_sleep_pending() && claims == 0);
}
static void test_dirty_settings_can_save_without_opening_sleep(void) {
    activity(ZMK_ACTIVITY_IDLE);
    dirty = true;
    assert(gmk_power_runtime_storage_safe());
    lease = true;
    assert(gmk_power_runtime_storage_safe()); /* Gate is called while its own lease is held. */
    lease = false;
    gmk_power_poll(true);
    assert(!gmk_power_sleep_pending());
    dirty = false;
}

static void test_native_pm_is_owned_by_exact_caller(void) {
    activity(ZMK_ACTIVITY_ACTIVE);
    activity(ZMK_ACTIVITY_IDLE);
    test_power_now += 60000;
    gmk_power_poll(true);
    assert(lease && paused && gmk_power_sleep_pending());
    test_power_thread = &activity_thread;
    assert(zmk_pm_suspend_devices() == 0);
    assert(test_pm_suspend_calls == 2 && test_native_pm_suspended[0] && test_native_pm_suspended[1]);
    test_power_thread = &service_thread;
    assert(zmk_pm_suspend_devices() == -EBUSY);
    zmk_pm_resume_devices();
    assert(test_pm_resume_calls == 0 && test_native_pm_suspended[0]);
    test_power_thread = &activity_thread;
    zmk_pm_resume_devices();
    assert(test_pm_resume_calls == 2 && !test_native_pm_suspended[0] && !test_native_pm_suspended[1]);
    test_power_thread = &service_thread;
    activity(ZMK_ACTIVITY_ACTIVE);
    gmk_power_poll(true);
    assert(!lease && !paused && !gmk_power_sleep_pending() && releases == 1);
}
static void test_peer_control_traffic_does_not_advertise_input_busy(void) {
    left_role        = false;
    peer_traffic     = true;
    wired_idle_state = false;
    activity(ZMK_ACTIVITY_IDLE);
    gmk_power_poll(true);
    uint8_t                        request[32], response[32];
    const struct gmk_power_command command = {.operation = GMK_POWER_PROBE, .session = 7};
    struct gmk_power_reply         reply;
    assert(gmk_power_protocol_request_encode(request, &command));
    gmk_power_receive(request, response);
    assert(gmk_power_protocol_reply_decode(request, response, &reply));
    assert(reply.phase == GMK_POWER_PHASE_IDLE && !reply.flags);
    assert(!gmk_power_runtime_storage_safe()); /* UART quiescence still gates storage. */
}

static void test_native_activity_enters_real_stop(void) {
    test_power_now = 30001;
    activity_work_handler(NULL);
    gmk_power_poll(true);
    assert(!gmk_power_sleep_pending());
    test_power_now    = 60001;
    test_power_thread = &activity_thread;
    activity_work_handler(NULL); /* Native suspend denied until service coordination. */
    assert(test_pm_suspend_calls == 0);
    test_power_thread = &service_thread;
    gmk_power_poll(true);
    assert(lease && paused && wake_prepares == 1);
    test_power_thread = &activity_thread;
    test_power_now    = 61001;
    test_basepri      = 0x80;
    if (!setjmp(reset_jump)) {
        activity_work_handler(NULL);
        assert(false);
    }
    assert(stops == 1 && resets == 1 && test_pm_suspend_calls == 2);
    assert(report_clears == 0 && !critical_prepare);
}

#ifdef CONFIG_GMK70_EVENT_IDLE
static void test_maintenance_does_not_move_sleep_deadline(void) {
    test_power_now = 30001;
    activity_work_handler(NULL);
    gmk_power_poll(true);
    for (test_power_now = 30002; test_power_now < 60001; ++test_power_now) {
        gmk_service_notify(); /* IRQ/status/heartbeat scheduling, no input edge. */
        gmk_power_poll(true);
        assert(!gmk_power_sleep_pending() && !claims);
    }
    assert(scheduling_notifications >= 29999);
    assert(gmk_power_delay() <= 20);
    gmk_power_poll(true);
    assert(gmk_power_sleep_pending() && paused && lease);
    test_power_thread = &activity_thread;
    test_power_now    = 61001;
    if (!setjmp(reset_jump)) {
        activity_work_handler(NULL);
        assert(false);
    }
    assert(stops == 1 && resets == 1 && !report_clears);
}
#endif

static void test_critical_reports_clear_once_and_held_key_bypass(void) {
    battery.percentage = 0;
    wired_idle_state   = false;
    gmk_power_poll(true);
    test_power_now   = 60000;
    radio_idle_state = false;
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        gmk_power_poll(true);
        ++test_power_now;
    }
    assert(report_clears == 1 && claims == 0);
    radio_idle_state = true;
    if (!setjmp(reset_jump)) {
        gmk_power_poll(true);
        assert(false);
    }
    assert(stops == 1 && resets == 1 && critical_prepare && report_clears == 1);
}

static void test_manual_release_does_not_cancel_its_own_request(void) {
#ifdef CONFIG_GMK70_EVENT_IDLE
    unsigned before = scheduling_notifications;
#endif
    assert(zmk_pm_soft_off() == 0);
#ifdef CONFIG_GMK70_EVENT_IDLE
    assert(scheduling_notifications == before + 1);
#endif
    power_test_listener->callback(&(zmk_event_t){.position = true});
    if (!setjmp(reset_jump)) {
        gmk_power_poll(true);
        assert(false);
    }
    assert(stops == 1 && resets == 1 && report_clears == 1);
}

static void test_new_input_cancels_forced_session(void) {
    assert(zmk_pm_soft_off() == 0);
    input_during_claim = true;
    gmk_power_poll(true);
    assert(stops == 0 && resets == 0 && releases == 1 && !lease);
    battery.valid = false;
    activity(ZMK_ACTIVITY_ACTIVE);
    input_during_claim = false;
    gmk_power_poll(true);
    assert(!gmk_power_sleep_pending());
}
static void test_usb_suspend_keeps_controller_and_returns_without_reset(void) {
    cable = usb_sleep = true;
    usb_status        = USB_DC_SUSPEND;
    activity(ZMK_ACTIVITY_IDLE);
    test_power_now = 90000;
    test_basepri   = 0x40;
    gmk_power_poll(true);
    assert(shallow_stops == (kernel_pending ? 0U : 1U) && stops == 0 && resets == 0);
    assert(systick == 7 && scr == 0x10 && test_basepri == 0x40 && test_primask == 0);
    assert(!lease && !paused && !gmk_power_sleep_pending());
    assert(test_pm_suspend_calls == 2 && test_pm_resume_calls == 2);
    assert(zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
    assert(mmio_write_count == 4); /* Only tick/SCR save/restore, no RCC or USB writes. */
    usb_status = USB_DC_SUSPEND;
    assert(zmk_pm_soft_off() == 0); /* Saved USB-sleep preference still admits a suspended host. */
}

static struct gmk_power_reply receive(enum gmk_power_operation operation, uint16_t session, bool critical) {
    uint8_t                        request[32], response[32];
    const struct gmk_power_command command = {.operation = operation, .session = session, .critical = critical};
    assert(gmk_power_protocol_request_encode(request, &command));
    gmk_power_receive(request, response);
    struct gmk_power_reply reply;
    assert(gmk_power_protocol_reply_decode(request, response, &reply));
    return reply;
}
static void prepare_right(bool critical) {
    left_role     = false;
    split_routing = true;
    dirty         = true; /* RIGHT must not wait on unowned persistent RGB/settings. */
    activity(ZMK_ACTIVITY_IDLE);
    gmk_power_poll(true);
    struct gmk_power_reply reply = receive(GMK_POWER_PREPARE, 7, critical);
    assert(reply.phase == GMK_POWER_PHASE_IDLE && claims == 0);
    gmk_power_poll(true);
    reply = receive(GMK_POWER_STATUS, 7, critical);
    assert(reply.phase == GMK_POWER_PHASE_READY && !reply.flags && lease && !paused);
}
static void test_right_commit_waits_and_pulses_peer(void) {
    prepare_right(false);
    (void)receive(GMK_POWER_COMMIT, 7, false);
    gmk_power_poll(true);
    assert(!paused && stops == 0);
    test_power_now = 249;
    gmk_power_poll(true);
    assert(!paused && stops == 0);
    test_power_now = 250;
    if (!setjmp(reset_jump)) {
        gmk_power_poll(true);
        assert(false);
    }
    assert(stops == 1 && resets == 1 && test_pm_suspend_calls == 2);
    if (rx_wakes) {
        assert(peer_pulses == 0 && mmio_write_count == 4 && exti_pending == (1U << 10));
        assert(mmio_writes[0].address == 0x40010c18U && mmio_writes[0].value == 0);
        assert(mmio_writes[1].address == 0x40010c1cU && mmio_writes[1].value == 128);
        assert(mmio_writes[2].address == 0x40010c20U && mmio_writes[2].value == 128);
        assert(mmio_writes[3].address == 0x40010c48U && mmio_writes[3].value == 0x8020);
    } else {
        const struct mmio_write expected[] = {
            {0x40010c18U, 0}, {0x40010c1cU, 128}, {0x40010c20U, 128}, {0x40010c48U, 0x8020}, {0x40000018U, 0x2000000}, {0x40000030U, 0xfdff}, {0x40000004U, 0}, {0x40000000U, 0x40000}, {0x40000018U, 0x200},
        };
        assert(peer_pulses == 1 && mmio_write_count == sizeof(expected) / sizeof(expected[0]));
        for (unsigned i = 0; i < mmio_write_count; ++i)
            assert(mmio_writes[i].address == expected[i].address && mmio_writes[i].value == expected[i].value);
        assert(exti_pending == (1U << 13));
    }
}
static void test_joined_routing_without_recent_peer_still_pulses_on_local_wake(void) {
    left_role     = false;
    split_routing = true;
    activity(ZMK_ACTIVITY_IDLE);
    test_power_now = 90000;
    if (!setjmp(reset_jump)) {
        gmk_power_poll(true); /* Stats report no recent peer; coordinator falls back locally. */
        assert(false);
    }
    assert(stops == 1 && resets == 1);
    assert(peer_pulses == (rx_wakes ? 0U : 1U));
}
static void test_cancelled_critical_session_does_not_suppress_activity(void) {
    prepare_right(true);
    (void)receive(GMK_POWER_CANCEL, 7, true);
    battery.valid = false;
    gmk_power_poll(true);
    assert(!lease && !gmk_power_sleep_pending());
    activity(ZMK_ACTIVITY_ACTIVE);
    gmk_power_poll(true);
    struct gmk_power_reply reply = receive(GMK_POWER_PROBE, 8, false);
    assert(reply.phase == GMK_POWER_PHASE_IDLE && (reply.flags & GMK_POWER_FLAG_ACTIVE));
}
static void test_new_input_cancels_critical_held_baseline(void) {
    battery.percentage = 0;
    wired_idle_state   = false;
    gmk_power_poll(true);
    test_power_now     = 60000;
    input_during_claim = true;
    gmk_power_poll(true);
    assert(stops == 0 && resets == 0 && releases == 1 && !lease);
    battery.valid = false;
    activity(ZMK_ACTIVITY_ACTIVE);
    input_during_claim = false;
    gmk_power_poll(true);
    assert(!gmk_power_sleep_pending());
}
static void test_native_partial_suspend_failure_keeps_exact_owner(void) {
    activity(ZMK_ACTIVITY_IDLE);
    test_power_now = 90000;
    gmk_power_poll(true);
    test_pm_fail      = true;
    test_power_thread = &activity_thread;
    assert(zmk_pm_suspend_devices() == -EIO);
    assert(test_pm_suspend_calls == 1 && test_native_pm_suspended[1]);
    test_power_thread = &service_thread;
    zmk_pm_resume_devices();
    assert(test_pm_resume_calls == 0);
    test_power_thread = &activity_thread;
    zmk_pm_resume_devices();
    assert(test_pm_resume_calls == 1 && !test_native_pm_suspended[1]);
}
static void test_activity_after_native_suspend_admission_skips_stop(void) {
    assert(zmk_pm_soft_off() == 0);
    test_pm_inject_activity = true;
    if (!setjmp(reset_jump)) {
        gmk_power_poll(true);
        assert(false);
    }
    assert(stops == 0 && resets == 1);
}

static void test_active_usb_soft_off_cannot_survive_until_unplug(void) {
    cable      = true;
    usb_status = USB_DC_CONFIGURED;
    assert(zmk_pm_soft_off() == -EBUSY);
    gmk_power_poll(true);
    assert(report_clears == 0 && claims == 0);
    power_test_listener->callback(&(zmk_event_t){.position = true});
    cable      = false;
    usb_status = USB_DC_DISCONNECTED;
    gmk_power_poll(true);
    assert(stops == 0 && resets == 0 && claims == 0 && report_clears == 0);
    assert(!gmk_power_sleep_pending());
}

#ifdef TEST_NATIVE_RGB
static struct k_work *runtime_rgb_save;
static unsigned       runtime_rgb_writes;
int                   settings_save_one(const char *name, const void *value, size_t size);
int                   k_work_submit(struct k_work *work) {
    runtime_rgb_save = work;
    return 1;
}
int gmk_zmk_settings_save_runtime(const char *name, const void *value, size_t size) {
    assert(!strcmp(name, "rgb/underglow/state") && value && size == 10);
    assert(((const uint8_t *)value)[8] == 1);
    assert(lease && gmk_power_runtime_storage_safe());
    ++runtime_rgb_writes;
    return 0;
}
static void test_critical_save_flushes_actual_rgb_dirt_while_keys_are_held(void) {
    battery.percentage = 0;
    wired_idle_state   = false;
    gmk_power_poll(true);
    test_power_now = 59000;
    gmk_power_poll(true);
    lease = true;
    assert(!gmk_power_runtime_storage_safe());
    lease          = false;
    test_power_now = 60000;
    assert(!zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0, .s = 100, .b = 20}));
    gmk_power_poll(true); /* Critical sleep must wait for the user's RGB snapshot. */
    assert(gmk_rgb_settings_dirty() && report_clears == 1 && claims == 0);
    lease = true;
    assert(gmk_power_runtime_storage_safe());
    battery.age_ms = 10001;
    assert(!gmk_power_runtime_storage_safe());
    battery.age_ms = 0;
    battery.valid  = false;
    assert(!gmk_power_runtime_storage_safe());
    battery.valid      = true;
    battery.percentage = 1;
    assert(!gmk_power_runtime_storage_safe());
    battery.percentage = 0;
    battery.cable      = true;
    assert(!gmk_power_runtime_storage_safe());
    battery.cable = false;
    cable         = true;
    assert(!gmk_power_runtime_storage_safe());
    cable             = false;
    test_power_thread = &activity_thread;
    assert(!gmk_power_runtime_storage_safe());
    test_power_thread = &service_thread;
    radio_idle_state  = false;
    assert(!gmk_power_runtime_storage_safe());
    radio_idle_state   = true;
    battery_idle_state = false;
    assert(!gmk_power_runtime_storage_safe());
    battery_idle_state = true;
    assert(gmk_power_runtime_storage_safe());
    /* Supply the native debounce callback's pinned ten-byte snapshot at its
     * existing60-second deadline, then execute the actual board runtime save. */
    test_power_now             = 120000;
    const uint8_t snapshot[10] = {0, 0, 100, 20, 1, 0, 0, 0, 1, 0};
    assert(settings_save_one("rgb/underglow/state", snapshot, sizeof(snapshot)) == 0);
    gmk_rgb_settings_poll(true);
    assert(runtime_rgb_save);
    runtime_rgb_save->handler(runtime_rgb_save);
    assert(runtime_rgb_writes == 1 && !gmk_rgb_settings_dirty());
    lease = false;
    if (!setjmp(reset_jump)) {
        gmk_power_poll(true);
        assert(false);
    }
    assert(stops == 1 && resets == 1 && critical_prepare && report_clears == 1);
}
#endif

#ifdef CONFIG_GMK70_RETAINED_SLEEP
static bool     retained_ready = true, retained_physical = true;
static unsigned retained_entries, refreshes;
uint32_t        gmk_retained_poll(void) {
    return UINT32_MAX;
}
bool gmk_retained_available(void) {
    return retained_ready;
}
bool gmk_retained_sleep(void) {
    assert(paused && radio_paused && lease && test_pm_suspend_calls == test_pm_resume_calls + 2);
    ++retained_entries;
    if (retained_physical) test_power_now += 3600000;
    return retained_physical;
}
void gmk_battery_request(void) {
    ++refreshes;
}
int k_work_submit(struct k_work *work) {
    work->handler(work);
    return 1;
}
static void test_returning_native_sleep(bool defer) {
    gmk_power_poll(true);
    test_power_now = 30000;
    activity(ZMK_ACTIVITY_IDLE);
    test_power_now    = 300001;
    retained_physical = !defer;
    gmk_power_poll(true);
    if (defer) {
        assert(retained_entries == 1 && paused && radio_paused && lease);
        assert(gmk_power_sleep_pending() && !resets && !stops && !refreshes);
        retained_physical = true;
        gmk_power_poll(true);
    }
    assert(retained_entries == (defer ? 2U : 1U) && !paused && !radio_paused && !lease);
    assert(!stops && !resets && report_clears == 0 && refreshes == 1);
    assert(!test_native_pm_suspended[0] && !test_native_pm_suspended[1]);
    assert(zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
    test_power_now += 1000;
    activity_work_handler(NULL); /* Native last-activity time was refreshed. */
    assert(zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
}
#endif

int main(int argc, char **argv) {
    assert(argc == 2);
#ifdef CONFIG_GMK70_RETAINED_SLEEP
    if (!strcmp(argv[1], "retained") || !strcmp(argv[1], "retained-defer")) {
        test_returning_native_sleep(!strcmp(argv[1], "retained-defer"));
        puts("Native returning sleep: ownership, quiet deferral, reports, settings and activity timestamp passed");
        return 0;
    }
    if (!strcmp(argv[1], "retained-unavailable")) {
        retained_ready = false;
        test_native_activity_enters_real_stop();
        return 0;
    }
#endif
#ifdef CONFIG_GMK70_EVENT_IDLE
    if (!strcmp(argv[1], "maintenance-stop")) {
        test_maintenance_does_not_move_sleep_deadline();
        return 0;
    }
#endif
    if (!strcmp(argv[1], "gates")) {
        test_native_pm_denies_early_or_usb_sleep();
        test_dirty_settings_can_save_without_opening_sleep();
        test_native_pm_is_owned_by_exact_caller();
    } else if (!strcmp(argv[1], "peer-traffic"))
        test_peer_control_traffic_does_not_advertise_input_busy();
    else if (!strcmp(argv[1], "native-stop"))
        test_native_activity_enters_real_stop();
    else if (!strcmp(argv[1], "critical"))
        test_critical_reports_clear_once_and_held_key_bypass();
    else if (!strcmp(argv[1], "manual"))
        test_manual_release_does_not_cancel_its_own_request();
    else if (!strcmp(argv[1], "new-input"))
        test_new_input_cancels_forced_session();
    else if (!strcmp(argv[1], "usb-shallow"))
        test_usb_suspend_keeps_controller_and_returns_without_reset();
    else if (!strcmp(argv[1], "right-commit"))
        test_right_commit_waits_and_pulses_peer();
    else if (!strcmp(argv[1], "right-rx")) {
        rx_wakes = true;
        test_right_commit_waits_and_pulses_peer();
    } else if (!strcmp(argv[1], "right-cancel"))
        test_cancelled_critical_session_does_not_suppress_activity();
    else if (!strcmp(argv[1], "usb-tick")) {
        kernel_pending = 1U << 26;
        test_usb_suspend_keeps_controller_and_returns_without_reset();
    } else if (!strcmp(argv[1], "usb-pendsv")) {
        kernel_pending = 1U << 28;
        test_usb_suspend_keeps_controller_and_returns_without_reset();
    } else if (!strcmp(argv[1], "new-critical-input"))
        test_new_input_cancels_critical_held_baseline();
    else if (!strcmp(argv[1], "pm-error"))
        test_native_partial_suspend_failure_keeps_exact_owner();
    else if (!strcmp(argv[1], "pm-activity-race"))
        test_activity_after_native_suspend_admission_skips_stop();
    else if (!strcmp(argv[1], "usb-blocked-manual"))
        test_active_usb_soft_off_cannot_survive_until_unplug();
    else if (!strcmp(argv[1], "fallback-local-wake"))
        test_joined_routing_without_recent_peer_still_pulses_on_local_wake();
    else if (!strcmp(argv[1], "fallback-rx-wake")) {
        rx_wakes = true;
        test_joined_routing_without_recent_peer_still_pulses_on_local_wake();
    } else if (!strcmp(argv[1], "usb-unplug-configured")) {
        native_usb_state = ZMK_USB_CONN_HID;
        usb_status       = USB_DC_CONFIGURED;
        test_native_activity_enters_real_stop();
    } else if (!strcmp(argv[1], "usb-unplug-suspended")) {
        native_usb_state = ZMK_USB_CONN_HID;
        usb_status       = USB_DC_SUSPEND;
        test_native_activity_enters_real_stop();
    } else if (!strcmp(argv[1], "usb-connected-hid"))
        test_physical_usb_preserves_native_hid_state();
#ifdef TEST_NATIVE_RGB
    else if (!strcmp(argv[1], "critical-settings"))
        test_critical_save_flushes_actual_rgb_dirt_while_keys_are_held();
#endif
    else
        assert(false);
    puts("Native power adapter gates passed");
    return 0;
}

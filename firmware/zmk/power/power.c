/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "power.h"
#include "coordinator.h"
#include "../wired/wired.h"
#include "../runtime/service.h"
#include "../runtime/idle.h"
#include "../battery/battery.h"
#include "../controls/controls.h"
#include "../function_flip/flip.h"
#include "../nkro/nkro.h"
#include "../lighting/rgb_settings.h"
#include "../lighting/lighting.h"
#include "../radio/radio.h"
#include "wb32_stop.h"
#ifdef CONFIG_GMK70_RETAINED_SLEEP
#    include "retained.h"
static uint32_t retained_delay = UINT32_MAX;
#    ifdef CONFIG_ZMK_POINTING
/* The pinned native activity work records its own last-activity timestamp.
 * A real wake may be a mode/encoder edge, not a synthetic key event. */
extern struct k_work note_activity_work;
#    endif
#endif
#include <errno.h>
#include <string.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/sys_io.h>
#include <zmk/activity.h>
#include <zmk/endpoints.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/pm.h>
#include <zmk/usb.h>
#if IS_ENABLED(CONFIG_ZMK_POINTING)
#    include <zephyr/input/input.h>
#endif

static struct gmk_power_core        core;
static struct gmk_power_coordinator coordinator;
static atomic_t                     mailbox, published, manual_request;
static k_tid_t                      pm_owner;
static k_tid_t                      service_owner;
static bool                         initialized, ready, pm_started, reports_cleared;
static bool                         session_active, session_activity;
enum { CACHE_READY = 128, CACHE_CRITICAL = 32 };
/* Pinned activity.c's state transition emits the ordinary native RGB policy. */
int set_state(enum zmk_activity_state state);

static bool active_critical(void) {
    return coordinator.phase != GMK_POWER_PHASE_IDLE && coordinator.critical;
}
static void begin_session(void) {
    unsigned key     = irq_lock();
    session_activity = false;
    session_active   = true;
    irq_unlock(key);
}
void gmk_power_note_activity(void) {
    unsigned key = irq_lock();
    if (session_active) {
        session_activity = true;
        atomic_clear(&manual_request);
    }
    gmk_power_core_activity(&core, true, k_uptime_get_32());
    atomic_set(&published, atomic_get(&published) & ~CACHE_READY);
    irq_unlock(key);
    gmk_service_notify();
}

bool gmk_power_usb_powered(void) {
    return (sys_read32(0x40000010U) & (1U << 7)) != 0;
}
enum zmk_usb_conn_state __real_zmk_usb_get_conn_state(void);
enum zmk_usb_conn_state __wrap_zmk_usb_get_conn_state(void) {
    /* The legacy controller cannot report physical unplug itself. Its last
     * CONFIGURED/SUSPEND state must not keep native activity USB-powered. */
    if (!gmk_power_usb_powered()) return ZMK_USB_CONN_NONE;
    enum zmk_usb_conn_state state = __real_zmk_usb_get_conn_state();
    return state == ZMK_USB_CONN_NONE ? ZMK_USB_CONN_POWERED : state;
}
static bool usb_shallow_allowed(void) {
    return gmk_power_usb_powered() && gmk_controls_usb_sleep() && zmk_usb_get_status() == USB_DC_SUSPEND;
}
static bool usb_blocks_sleep(void) {
    return gmk_power_usb_powered() && !usb_shallow_allowed();
}
static bool radio_idle(void) {
#ifdef CONFIG_GMK70_RADIO
    return gmk_radio_idle();
#else
    return true;
#endif
}
static void radio_wake(void) {
#ifdef CONFIG_GMK70_RADIO
    gmk_radio_wake();
#endif
}
static bool preferences_dirty(void) {
    if (!gmk_wired_is_left()) return false; /* RIGHT has no persistent owner. */
    bool dirty = false;
#ifdef CONFIG_GMK70_CONTROLS
    dirty |= gmk_controls_dirty();
#endif
#ifdef CONFIG_GMK70_FUNCTION_FLIP
    dirty |= gmk_flip_dirty();
#endif
#ifdef CONFIG_GMK70_NKRO
    dirty |= gmk_nkro_dirty();
#endif
#if defined(CONFIG_GMK70_ZMK_STUDIO_SETTINGS) && defined(CONFIG_GMK70_WB32_RGB)
    dirty |= gmk_rgb_settings_dirty();
#endif
    return dirty;
}
static bool busy(bool own_lease) {
    return preferences_dirty() || !radio_idle() || !gmk_battery_idle() || (!own_lease && !gmk_zmk_service_idle());
}
bool gmk_power_sleep_pending(void) {
    return ((uint32_t)atomic_get(&published) & 3U) != GMK_POWER_PHASE_IDLE;
}
static struct gmk_power_inputs battery_inputs(void) {
    struct gmk_battery_status battery;
    gmk_battery_get(&battery);
    return (struct gmk_power_inputs){.now_ms = k_uptime_get_32(), .battery_age_ms = battery.age_ms, .battery_percentage = battery.percentage, .battery_valid = battery.valid, .cable = battery.cable, .usb_powered = gmk_power_usb_powered()};
}
bool gmk_power_runtime_storage_safe(void) {
    /* This caller already owns the receiver lease and may be saving dirty
     * preferences. Testing either here would permanently prevent that save. */
    if (k_current_get() != service_owner || !gmk_wired_is_left() || gmk_power_sleep_pending() || !radio_idle() || !gmk_battery_idle()) return false;
    if (zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE && gmk_wired_power_idle()) return true;
    /* Only the existing allowlisted runtime-preference backend calls this
     * gate. Qualified critical power may flush those preferences despite held
     * keys; no updater, raw flash or arbitrary settings admission changes. */
    const struct gmk_power_inputs inputs = battery_inputs();
    return gmk_power_core_critical_ready(&core, &inputs);
}

static bool claim(struct gmk_power_platform *context) {
    ARG_UNUSED(context);
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    return gmk_zmk_storage_claim();
#else
    return false; /* No atomic upload/settings exclusion on this configuration. */
#endif
}
static void release(struct gmk_power_platform *context) {
    ARG_UNUSED(context);
#ifdef CONFIG_GMK70_ZMK_STUDIO_SETTINGS
    gmk_zmk_storage_release();
#endif
}
static bool quiesce(struct gmk_power_platform *context, bool enabled) {
    ARG_UNUSED(context);
    bool ok = true;
#ifdef CONFIG_GMK70_RADIO
    ok = gmk_radio_power_quiesce(enabled);
#endif
    bool battery  = gmk_battery_power_quiesce(enabled);
    bool lighting = gmk_lighting_power_quiesce(enabled);
    return battery && lighting && ok;
}
static bool prepare_wake(struct gmk_power_platform *context) {
    ARG_UNUSED(context);
    return gmk_wired_power_prepare(coordinator.critical);
}
static void cancel_wake(struct gmk_power_platform *context) {
    ARG_UNUSED(context);
    gmk_wired_power_resume();
}
static bool wake_pending(struct gmk_power_platform *context) {
    ARG_UNUSED(context);
    return session_activity || usb_blocks_sleep() || (!active_critical() && !atomic_get(&manual_request) && !core.inactive) || (coordinator.armed && gmk_wired_power_wake_pending());
}
static bool peer_exchange(struct gmk_power_platform *context, const struct gmk_power_command *command, struct gmk_power_reply *reply) {
    ARG_UNUSED(context);
    uint8_t request[32], response[32];
    return gmk_power_protocol_request_encode(request, command) && gmk_wired_split_command(request, response) && gmk_power_protocol_reply_decode(request, response, reply);
}
static const struct gmk_power_ops ops = {.claim_storage = claim, .release_storage = release, .quiesce = quiesce, .prepare_wake = prepare_wake, .cancel_wake = cancel_wake, .wake_pending = wake_pending, .peer_exchange = peer_exchange};

static int activity_changed(const zmk_event_t *event) {
    if (as_zmk_position_state_changed(event)) {
        gmk_power_note_activity();
        return ZMK_EV_EVENT_BUBBLE;
    }
    const struct zmk_activity_state_changed *activity = as_zmk_activity_state_changed(event);
    if (!activity) return ZMK_EV_EVENT_BUBBLE;
    unsigned key    = irq_lock();
    bool     active = activity->state == ZMK_ACTIVITY_ACTIVE;
    gmk_power_core_activity(&core, active, k_uptime_get_32() - (active ? 0 : CONFIG_ZMK_IDLE_TIMEOUT));
    if (active) {
        gmk_power_note_activity();
    }
    irq_unlock(key);
    gmk_service_notify();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(gmk_power_activity, activity_changed);
ZMK_SUBSCRIPTION(gmk_power_activity, zmk_activity_state_changed);
ZMK_SUBSCRIPTION(gmk_power_activity, zmk_position_state_changed);
#if IS_ENABLED(CONFIG_ZMK_POINTING)
static void pointing_activity(struct input_event *event, void *context) {
    ARG_UNUSED(event);
    ARG_UNUSED(context);
    gmk_power_note_activity();
}
INPUT_CALLBACK_DEFINE(NULL, pointing_activity, NULL);
#endif

/* Preserve the linked integration witness under LTO. */
__attribute__((noipa)) void gmk_power_receive(const uint8_t request[32], uint8_t response[32]) {
    memset(response, 0, 32);
    struct gmk_power_command command;
    if (!initialized || gmk_wired_is_left() || !gmk_power_protocol_request_decode(request, &command)) return;
    uint32_t               snapshot = (uint32_t)atomic_get(&published);
    struct gmk_power_reply reply    = {.phase = snapshot & 3U, .flags = (snapshot >> 2) & 7U};
    bool                   matches  = command.session == (uint16_t)(snapshot >> 8) && command.critical == ((snapshot & CACHE_CRITICAL) != 0);
    if (command.operation == GMK_POWER_STATUS && !matches) reply.flags |= GMK_POWER_FLAG_BUSY;
    if (command.operation == GMK_POWER_PREPARE || command.operation == GMK_POWER_COMMIT || command.operation == GMK_POWER_CANCEL) {
        uint32_t     value = ((uint32_t)command.session << 8) | (command.critical ? 16U : 0) | ((uint32_t)command.operation + 1U);
        atomic_val_t old   = atomic_get(&mailbox);
        if ((old && (uint32_t)old != value) || (!old && !atomic_cas(&mailbox, 0, (atomic_val_t)value))) reply.flags |= GMK_POWER_FLAG_BUSY;
        gmk_service_notify();
    }
    (void)gmk_power_protocol_reply_encode(response, request, &reply);
}

int  __real_zmk_pm_suspend_devices(void);
void __real_zmk_pm_resume_devices(void);
int  __wrap_zmk_pm_suspend_devices(void) {
    unsigned key = irq_lock();
    /* READY was published by the UART/storage owner after quiescence. This
     * native activity caller must not inspect that owner's parser state. */
#ifdef CONFIG_GMK70_RETAINED_SLEEP
    /* Returning suspend belongs to the service owner; native activity may
     * request policy but must not enter its non-returning sys_poweroff path. */
    if (gmk_retained_available() && !active_critical() && k_current_get() != service_owner) {
        irq_unlock(key);
        return -EBUSY;
    }
#endif
    if (!((uint32_t)atomic_get(&published) & CACHE_READY) || usb_blocks_sleep() || gmk_wired_power_wake_pending() || pm_owner) {
        irq_unlock(key);
        return -EBUSY;
    }
    pm_owner   = k_current_get();
    pm_started = true;
    irq_unlock(key);
    return __real_zmk_pm_suspend_devices();
}
void __wrap_zmk_pm_resume_devices(void) {
    unsigned key   = irq_lock();
    bool     owner = pm_owner && pm_owner == k_current_get();
    irq_unlock(key);
    if (!owner) return;
    if (pm_started) __real_zmk_pm_resume_devices();
    key        = irq_lock();
    pm_started = false;
    pm_owner   = NULL;
    irq_unlock(key);
}
int __wrap_zmk_pm_soft_off(void) {
    if (k_is_in_isr()) return -EWOULDBLOCK;
    if (usb_blocks_sleep()) return -EBUSY;
    atomic_set(&manual_request, 1);
    gmk_service_notify();
    return 0;
}

static void publish(bool pending_ready, bool is_busy) {
    uint32_t flags = (gmk_power_usb_powered() ? GMK_POWER_FLAG_USB : 0) | (is_busy || !ready ? GMK_POWER_FLAG_BUSY : 0) | (!core.inactive && !active_critical() && !atomic_get(&manual_request) ? GMK_POWER_FLAG_ACTIVE : 0);
    uint32_t value = ((uint32_t)coordinator.session << 8) | (pending_ready ? CACHE_READY : 0) | (coordinator.critical ? CACHE_CRITICAL : 0) | (flags << 2) | (uint32_t)coordinator.phase;
    atomic_set(&published, (atomic_val_t)value);
}

static void usb_shallow_sleep(void) {
    /* Reversible service-owned sleep: retain the legacy USB controller and its
     * clocks. Freeze only the kernel tick and clear SLEEPDEEP, preserving all
     * pending IRQs. Native HID processes a key wake after normal scanning
     * resumes and supplies host-authorized remote wake through usb_dc. */
    uint32_t primask = __get_PRIMASK(), basepri = __get_BASEPRI();
    __disable_irq();
    __set_BASEPRI(0);
    uint32_t systick = sys_read32(0xe000e010U), scr = sys_read32(0xe000ed10U);
    sys_write32(systick & ~1U, 0xe000e010U);
    sys_write32(scr & ~4U, 0xe000ed10U);
    __DSB();
    __ISB();
    /* A preexisting kernel tick/context switch must be serviced, not cleared
     * as if it were stale. Preserve the flags and skip this idle attempt. */
    bool kernel_pending = (sys_read32(0xe000ed04U) & ((1U << 26) | (1U << 28))) != 0;
    if (!kernel_pending && !wake_pending(NULL) && !busy(coordinator.claimed)) __WFI();
    atomic_set(&published, atomic_get(&published) & ~CACHE_READY);
    sys_write32(scr, 0xe000ed10U);
    sys_write32(systick, 0xe000e010U);
    __set_BASEPRI(basepri);
    __set_PRIMASK(primask);
    zmk_pm_resume_devices();
    if (gmk_wired_power_wake_pending()) radio_wake();
    gmk_power_coordinator_cancel(&coordinator, &ops);
    atomic_clear(&manual_request);
    session_active = session_activity = false;
    (void)set_state(ZMK_ACTIVITY_ACTIVE);
    publish(false, false);
}

/* Preserve the linked safety boundary under LTO. */
__attribute__((noipa)) void gmk_power_poll(bool service_ready) {
#ifdef CONFIG_GMK70_RETAINED_SLEEP
    retained_delay = gmk_retained_poll();
#endif
    ready = service_ready;
    if (!initialized) {
        service_owner = k_current_get();
        (void)gmk_power_coordinator_init(&coordinator, gmk_wired_is_left() ? GMK_POWER_ROLE_LEFT : GMK_POWER_ROLE_RIGHT);
        initialized = true;
    }
    uint32_t now     = k_uptime_get_32();
    uint32_t pending = (uint32_t)atomic_clear(&mailbox);
    if (pending) {
        const struct gmk_power_command command = {.operation = (pending & 7U) - 1U, .session = (uint16_t)(pending >> 8), .critical = (pending & 16U) != 0};
        if (command.operation == GMK_POWER_PREPARE && coordinator.phase == GMK_POWER_PHASE_IDLE) begin_session();
        (void)gmk_power_coordinator_command(&coordinator, &command, now, &ops);
    }
#ifdef CONFIG_GMK70_RADIO
    atomic_val_t previous_request;
    if (!usb_blocks_sleep() && gmk_radio_disconnected_timeout())
        previous_request = atomic_or(&manual_request, 2);
    else
        previous_request = atomic_and(&manual_request, ~2);
#endif
    bool                    manual = atomic_get(&manual_request) != 0;
    struct gmk_power_inputs inputs = battery_inputs();
    inputs.timeout_ms              = gmk_controls_sleep_timeout_ms();
    inputs.usb_sleep_allowed       = usb_shallow_allowed();
    inputs.keys_held               = !gmk_wired_power_input_idle();
    unsigned               key     = irq_lock();
    enum gmk_power_request reason  = gmk_power_core_decide(&core, &inputs);
    irq_unlock(key);
    if (manual && !usb_blocks_sleep()) reason = GMK_POWER_INACTIVITY;
    bool forced = manual || reason == GMK_POWER_CRITICAL || active_critical();
    if (forced && !reports_cleared) {
        reports_cleared = true;
        zmk_endpoint_clear_reports();
        (void)set_state(ZMK_ACTIVITY_SLEEP);
    } else if (!forced && !core.critical) {
        reports_cleared = false;
    }
#ifdef CONFIG_GMK70_RADIO
    /* Reconnecting retires a pending disconnected-only sleep. It does not
     * count as input or cancel deliberate/critical/ordinary idle sleep. */
    if ((previous_request & 2) && !manual && reason == GMK_POWER_NONE) gmk_power_coordinator_cancel(&coordinator, &ops);
#endif
    bool is_busy = busy(coordinator.claimed) || (!forced && !gmk_wired_power_input_idle());
    bool allowed = ready && !usb_blocks_sleep() && (forced || core.inactive);
    if (coordinator.phase == GMK_POWER_PHASE_IDLE && reason != GMK_POWER_NONE && allowed && !is_busy) {
        struct gmk_wired_stats stats;
        bool                   have_peer = gmk_split_available() && (!gmk_wired_try_get_stats(&stats) || stats.peer);
        begin_session();
        (void)gmk_power_coordinator_request(&coordinator, reason, have_peer, now);
    }
    gmk_power_coordinator_step(&coordinator, now, allowed, is_busy, &ops);
    if (coordinator.phase == GMK_POWER_PHASE_IDLE) session_active = session_activity = false;
    bool prepared_ready = gmk_power_coordinator_ready(&coordinator) || gmk_power_coordinator_committed(&coordinator, now);
    if (prepared_ready) {
        gmk_wired_power_pause(true);
        prepared_ready = !wake_pending(NULL) && !busy(coordinator.claimed);
        if (!prepared_ready) gmk_power_coordinator_cancel(&coordinator, &ops);
    }
    publish(prepared_ready, is_busy);
    /* Ordinary LEFT inactivity enters through pinned native activity.c. RIGHT
     * commit and deliberate/critical requests use the same native PM API path
     * from this sole service owner, never a serial/USB/input callback. */
    bool returning = false;
#ifdef CONFIG_GMK70_RETAINED_SLEEP
    returning = gmk_retained_available() && !coordinator.critical;
#endif
    if (prepared_ready && (returning || forced || coordinator.role == GMK_POWER_ROLE_RIGHT || usb_shallow_allowed())) {
        enum zmk_activity_state before = zmk_activity_get_state();
        if (returning) (void)set_state(ZMK_ACTIVITY_SLEEP);
        int result = zmk_pm_suspend_devices();
        if (!result) {
            if (usb_shallow_allowed()) {
                usb_shallow_sleep();
                return;
            }
#ifdef CONFIG_GMK70_RETAINED_SLEEP
            if (returning) {
                bool physical = gmk_retained_sleep();
                if (!physical && gmk_retained_available()) {
                    /* A pending kernel/maintenance IRQ only defers STOP. Keep
                     * both peers quiescent; do not send CANCEL or fake activity. */
                    zmk_pm_resume_devices();
                    return;
                }
                if (!physical) sys_poweroff(); /* RTC failed: existing reset fallback. */
                atomic_set(&published, atomic_get(&published) & ~CACHE_READY);
                zmk_pm_resume_devices();
                if (physical) radio_wake();
                gmk_power_coordinator_cancel(&coordinator, &ops);
                atomic_clear(&manual_request);
                session_active = session_activity = false;
                if (physical) {
                    gmk_power_note_activity();
#    ifdef CONFIG_ZMK_POINTING
                    k_work_submit(&note_activity_work);
#    endif
                }
                (void)set_state(physical ? ZMK_ACTIVITY_ACTIVE : before);
                gmk_battery_request();
                publish(false, false);
                return;
            }
#endif
            sys_poweroff();
        }
        zmk_pm_resume_devices();
        if (returning) (void)set_state(before);
        gmk_power_coordinator_cancel(&coordinator, &ops);
        publish(false, true);
    }
}

#ifdef CONFIG_GMK70_EVENT_IDLE
uint32_t gmk_power_delay(void) {
    if (coordinator.phase != GMK_POWER_PHASE_IDLE || atomic_get(&mailbox) || atomic_get(&manual_request)) return 1;
    uint32_t now = k_uptime_get_32(), delay = 1000;
#    ifdef CONFIG_GMK70_RETAINED_SLEEP
    if (retained_delay < delay) delay = retained_delay;
#    endif
    unsigned key = irq_lock();
    if (core.inactive) {
        uint32_t timeout = gmk_controls_sleep_timeout_ms();
        uint32_t elapsed = now - core.inactive_since;
        if (timeout) {
            uint32_t next = elapsed >= timeout ? 20 : timeout - elapsed;
            if (next < delay) delay = next;
        }
    }
    irq_unlock(key);
    return delay;
}
#endif

/* Preserve the linked safety boundary under LTO. */
__attribute__((noipa)) _Noreturn void z_sys_poweroff(void) {
    /* Capture routing while GPIO clocks are valid. Peer health may already
     * have timed out independently on both otherwise-connected halves. */
    bool physical_split = gmk_split_available();
    /* Zephyr's irq_lock uses BASEPRI on Cortex-M3. Vendor STOP requires hard
     * IRQ masking with enabled IRQs still pending: PRIMASK1 then BASEPRI0. */
    __disable_irq();
    __set_BASEPRI(0);
    __DSB();
    __ISB();
    if (((uint32_t)atomic_get(&published) & CACHE_READY) && pm_owner == k_current_get() && !gmk_power_usb_powered() && !gmk_wired_power_wake_pending()) wb32_stop_hardware();
    /* Reestablish the exact WB32 AHB/APB source gates used by soc_early_init
     * before touching GPIO after LP4. Preserve EXTI/NVIC pending wake causes. */
    if (physical_split) {
        sys_write32(0, 0x40010c18U);
        sys_write32(1U << 7, 0x40010c1cU);
        sys_write32(1U << 7, 0x40010c20U);
        sys_set_bits(0x40010c48U, (1U << 15) | (1U << 5));
        if (!(sys_read32(0x40001814U) & (1U << 10))) {
            sys_write32(1U << 25, 0x40000018U); /* A9 latch low before output mode. */
            sys_write32((uint16_t)~(1U << 9), 0x40000030U);
            sys_write32(0, 0x40000004U);
            sys_write32(1U << 18, 0x40000000U);
            k_busy_wait(100U); /* Same nominal pulse as maintained QMK split wake. */
            sys_write32(1U << 9, 0x40000018U);
        }
    }
    /* sys_poweroff cannot return. A raced wake skips STOP and follows this
     * same normal reset path; permanent firmware never arms a watchdog. */
    sys_reboot(SYS_REBOOT_WARM);
}

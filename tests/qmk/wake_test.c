// SPDX-License-Identifier: GPL-2.0-or-later
// Maintained WB32 and board hooks with mocked registers, not physical STOP/RF.
#include <assert.h>
#include <stdio.h>
#include "quantum.h"
#include "wireless.h"

test_event_t           _pal_events[16];
test_exti_t            test_exti;
test_nvic_t            test_nvic;
test_systick_t         test_systick;
test_scb_t             test_scb;
test_pwr_t             test_pwr;
bool                   lower_sleep;
static bool            master, cable, peer_cable, enabled[64];
static unsigned        edges[48], input[48], low[48], last_disabled, sends, drains, delay;
static uint8_t         device, radio_state;
static lpwr_prepare_t  preparation;
static lpwr_wakeupcd_t cause;
static lpwr_state_t    state, previous;
lpwr_state_t           lpwr_get_previous_state(void) {
    return previous;
}

void lpwr_exti_init(void);
void lpwr_stop_prepare(void);
bool lpwr_stop_pending(void);
void lpwr_stop_hook_pre(void);
void lpwr_stop_hook_post(void);
bool is_keyboard_master(void) {
    return master;
}
bool gpio_read_pin(ioline_t pin) {
    assert(pin == A7);
    return cable;
}
bool gmk70_stock_cable_flag(void) {
    return cable || peer_cable;
}
void gpio_set_pin_output_open_drain(ioline_t pin) {
    assert(pin < 48);
    input[pin] = 4;
}
void gpio_set_pin_input_low(ioline_t pin) {
    assert(pin < 48);
    input[pin] = 3;
}
void gpio_set_pin_input_high(ioline_t pin) {
    assert(pin < 48);
    input[pin] = 2;
}
void gpio_set_pin_input(ioline_t pin) {
    assert(pin < 48);
    input[pin] = 1;
}
void gpio_write_pin_low(ioline_t pin) {
    assert(pin < 48);
    ++low[pin];
}
void waitInputPinDelay(void) {}
void palDisableLineEventI(ioline_t pin) {
    edges[pin]                = 0;
    _pal_events[PAL_PAD(pin)] = (test_event_t){0};
}
void palEnableLineEventI(ioline_t pin, uint32_t edge) {
    assert(edges[pin] == 0);
    assert(_pal_events[PAL_PAD(pin)].cb);
    edges[pin] = edge;
}
void nvicEnableVector(unsigned vector, unsigned priority) {
    assert(vector < 64 && priority == 6);
    enabled[vector] = true;
}
void nvicDisableVector(unsigned vector) {
    assert(vector < 64);
    last_disabled   = vector;
    enabled[vector] = false;
}
void           __early_init(void) {}
void           matrix_init_pins(void) {}
lpwr_prepare_t lpwr_get_prepare_class(void) {
    return preparation;
}
void lpwr_set_prepare_class(lpwr_prepare_t value) {
    preparation = value;
}
lpwr_wakeupcd_t lpwr_get_sleep_wakeupcd(void) {
    return cause;
}
void lpwr_set_sleep_wakeupcd(lpwr_wakeupcd_t value) {
    cause = value;
}
void lpwr_set_state(lpwr_state_t value) {
    previous = state;
    state    = value;
}
void gmk70_stock_usb_sleep_set(bool requested) {
    assert(!requested);
}
uint8_t wireless_get_current_devs(void) {
    return device;
}
uint8_t *md_getp_state(void) {
    return &radio_state;
}
void md_send_devctrl(uint8_t command) {
    assert(command == MD_SND_CMD_DEVCTRL_USB);
    ++sends;
}
void md_wait_for_commands(void) {
    assert(sends > drains);
    ++drains;
}
void wait_ms(unsigned ms) {
    assert(drains == sends);
    delay += ms;
}
static void reset(bool is_master, lpwr_prepare_t cls) {
    memset(edges, 0, sizeof(edges));
    memset(input, 0, sizeof(input));
    memset(low, 0, sizeof(low));
    memset(enabled, 0, sizeof(enabled));
    master      = is_master;
    preparation = cls;
    cause       = LPWR_WAKEUP_NONE;
    state       = LPWR_STOP;
    previous    = LPWR_PRESLEEP;
    cable       = false;
    peer_cable  = false;
    lower_sleep = cls == LPWR_PREPARE_CRITICAL;
    device      = DEVS_BT1;
    radio_state = MD_STATE_CONNECTED;
    sends = drains = delay = 0;
    lpwr_stop_prepare();
}
static void fire(unsigned line) {
    assert(_pal_events[line].cb);
    _pal_events[line].cb(_pal_events[line].arg);
}
static void test_wake_sources(void) {
    for (unsigned role = 0; role < 2; ++role) {
        for (lpwr_prepare_t cls = LPWR_PREPARE_ORDINARY; cls <= LPWR_PREPARE_CRITICAL; ++cls) {
            reset(role, cls);
            lpwr_exti_init();
            assert(edges[A10] == 3 && edges[A7] == 1 && edges[C11] == 0);
            assert(input[A10] == 1 && input[B6] == 0);
            assert(edges[B6] == (cls == LPWR_PREPARE_ORDINARY ? 3u : 0u));
            assert(edges[C13] == (cls != LPWR_PREPARE_CRITICAL ? 3u : 0u));
            assert(edges[C14] == (master ? 3u : 0u) && edges[C15] == (master ? 3u : 0u));
            assert(edges[A1] == (cls == LPWR_PREPARE_ORDINARY ? 3u : 0u));
            assert(enabled[EXTI0_IRQn + 1] == (cls == LPWR_PREPARE_ORDINARY));
            assert(!enabled[EXTI0_IRQn]); // No blanket irqInit on unused groups.
            assert(enabled[EXTI9_5_IRQn] && enabled[EXTI15_10_IRQn]);
            if (cls == LPWR_PREPARE_ORDINARY) {
                assert(input[master ? C0 : B13] == 4 && low[master ? C0 : B13] == 1);
                fire(PAL_PAD(B6));
                assert(cause == LPWR_WAKEUP_MATRIX);
                assert(last_disabled == EXTI9_5_IRQn && test_exti.PR == 0x3E0);
                assert(enabled[EXTI0_IRQn + 1] && enabled[EXTI15_10_IRQn]);
                assert(lpwr_stop_pending());
            } else if (cls == LPWR_PREPARE_USB) {
                assert(low[master ? C3 : C7] == 1);
                assert(low[master ? C0 : B13] == 0 && input[master ? C0 : B13] == 2 && input[A1] == 3);
            } else {
                assert(input[master ? C0 : B13] == 2 && input[A1] == 3);
            }
            // Repeated arming must replace callbacks cleared by PAL disable.
            cause = LPWR_WAKEUP_NONE;
            lpwr_stop_prepare();
            assert(!lpwr_stop_pending());
            lpwr_exti_init();
            fire(PAL_PAD(A10));
            assert(cause == LPWR_WAKEUP_MATRIX && last_disabled == EXTI15_10_IRQn && test_exti.PR == 0xFC00);
            assert(enabled[EXTI9_5_IRQn]);
        }
    }
    reset(true, LPWR_PREPARE_ORDINARY);
    lpwr_exti_init();
    fire(PAL_PAD(A1));
    assert(cause == LPWR_WAKEUP_MATRIX && last_disabled == EXTI0_IRQn + 1 && test_exti.PR == (1u << 1));
    assert(enabled[EXTI9_5_IRQn] && enabled[EXTI15_10_IRQn]);
    fire(PAL_PAD(C13));
    assert(cause == LPWR_WAKEUP_ONEKEY);
}
static void test_special_wake_filter(void) {
    for (lpwr_prepare_t cls = LPWR_PREPARE_USB; cls <= LPWR_PREPARE_CRITICAL; ++cls) {
        for (lpwr_wakeupcd_t wake = LPWR_WAKEUP_NONE; wake <= LPWR_WAKEUP_SWITCH; ++wake) {
            reset(false, cls);
            cause = wake;
            lpwr_stop_hook_post();
            bool accepted = wake == LPWR_WAKEUP_USB || wake == LPWR_WAKEUP_CABLE;
            assert(state == (accepted ? LPWR_WAKEUP : LPWR_STOP));
            assert(preparation == (accepted ? LPWR_PREPARE_ORDINARY : cls));
            assert(lower_sleep == (cls == LPWR_PREPARE_CRITICAL && !accepted));
        }
    }
}
static void test_radio_before_stop(void) {
    reset(true, LPWR_PREPARE_ORDINARY);
    lpwr_stop_hook_pre();
    assert(low[A5] == 1 && low[A8] == 1 && low[A9] == 0 && sends == 0);
    radio_state = MD_STATE_DISCONNECTED;
    lpwr_stop_hook_pre();
    assert(sends == 1 && drains == 1 && delay == 300);
    reset(true, LPWR_PREPARE_CRITICAL);
    lpwr_stop_hook_pre();
    assert(sends == 1 && drains == 1 && delay == 0);
    previous = LPWR_STOP;
    cause    = LPWR_WAKEUP_NONE;
    lpwr_stop_hook_pre();
    assert(sends == 1);
    previous = LPWR_PRESLEEP;
    cause    = LPWR_WAKEUP_UART;
    lpwr_stop_hook_pre();
    assert(sends == 2);
    reset(true, LPWR_PREPARE_USB);
    device = DEVS_USB;
    cable  = true;
    lpwr_stop_hook_pre();
    assert(sends == 0 && delay == 0);
    reset(false, LPWR_PREPARE_ORDINARY);
    device      = DEVS_USB;
    peer_cable  = true;
    radio_state = MD_STATE_DISCONNECTED;
    lpwr_stop_hook_pre();
    assert(sends == 0 && !cable);
}
int main(void) {
    test_wake_sources();
    test_special_wake_filter();
    test_radio_before_stop();
    puts("WB32 wake sources, per-group IRQs, repeat arming, special filters and radio STOP sequencing passed");
}

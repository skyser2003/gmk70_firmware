// SPDX-License-Identifier: GPL-2.0-or-later
// Actual pinned native matrix/debounce/split code with simulated GPIO and IRQs.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "quantum.h"
#include "lowpower.h"

palevent_t          _pal_events[16];
test_exti_t         test_exti;
bool                isLeftHand;
extern matrix_row_t matrix[MATRIX_ROWS];
extern uint8_t      thisHand, thatHand;
static const pin_t  rows[5]       = MATRIX_ROW_PINS;
static const pin_t  left_cols[9]  = MATRIX_COL_PINS;
static const pin_t  right_cols[9] = MATRIX_COL_PINS_RIGHT;
static bool         left, master, connected, driven[64], down[5][9];
static bool         edge_on_drive, pulse_on_unlock;
static unsigned     reads, drives, hooks, split_calls, locks;
static bool         vectors[7];
static uint32_t     now;
static matrix_row_t peer[5];
static lpwr_state_t power_state;

bool is_keyboard_left(void) {
    return left;
}
bool is_keyboard_master(void) {
    return master;
}
lpwr_state_t lpwr_get_state(void) {
    return power_state;
}
uint16_t timer_read_fast(void) {
    return now;
}
uint16_t timer_elapsed_fast(uint16_t time) {
    return (uint16_t)(now - time);
}
uint32_t timer_read32(void) {
    return now;
}
uint32_t timer_elapsed32(uint32_t time) {
    return now - time;
}
void wait_us(uint16_t us) {
    (void)us;
}
void waitInputPinDelay(void) {}
void chSysLock(void) {
    ++locks;
}
static void fire_line(unsigned line) {
    unsigned vector = line <= 4 ? line : line <= 9 ? EXTI9_5_IRQn : EXTI15_10_IRQn;
    if (vectors[vector] && (EXTI->IMR & (1U << line)) && _pal_events[line].cb) _pal_events[line].cb(_pal_events[line].arg);
}
static void fire(unsigned row) {
    fire_line(PAL_PAD(rows[row]));
}
void chSysUnlock(void) {
    assert(locks);
    --locks;
    if (pulse_on_unlock && !locks && EXTI->IMR) {
        pulse_on_unlock = false;
        fire(0);
    }
}
void palEnableLineEventI(pin_t pin, uint32_t edges) {
    assert(locks && edges == PAL_EVENT_MODE_FALLING_EDGE);
    unsigned bit = 1U << PAL_PAD(pin);
    assert(!(EXTI->IMR & bit));
    EXTI->IMR |= bit;
    EXTI->FTSR |= bit;
}
void palDisableLineEventI(pin_t pin) {
    assert(locks);
    unsigned bit = 1U << PAL_PAD(pin);
    EXTI->IMR &= ~bit;
    EXTI->FTSR &= ~bit;
    EXTI->RTSR &= ~bit;
    _pal_events[PAL_PAD(pin)] = (palevent_t){0};
}
void nvicEnableVector(unsigned vector, unsigned priority) {
    assert(vector <= EXTI15_10_IRQn && priority == 6);
    vectors[vector] = true;
}
void gpio_write_pin_low(pin_t pin) {
    driven[pin] = true;
    ++drives;
    if (edge_on_drive && EXTI->IMR) {
        edge_on_drive = false;
        down[0][0]    = true;
    }
}
void gpio_write_pin_high(pin_t pin) {
    driven[pin] = false;
}
void gpio_set_pin_output(pin_t pin) {
    (void)pin;
}
void gpio_set_pin_output_open_drain(pin_t pin) {
    (void)pin;
}
void gpio_set_pin_input_high(pin_t pin) {
    driven[pin] = false;
}
bool gpio_read_pin(pin_t pin) {
    ++reads;
    const pin_t *cols = left ? left_cols : right_cols;
    for (unsigned r = 0; r < 5; ++r)
        if (pin == rows[r])
            for (unsigned c = 0; c < 9; ++c)
                if (cols[c] != NO_PIN && driven[cols[c]] && down[r][c]) return false;
    return true;
}
bool transport_master_if_connected(matrix_row_t local[], matrix_row_t remote[]) {
    (void)local;
    ++split_calls;
    if (connected) memcpy(remote, peer, sizeof(peer));
    return connected;
}
bool transport_master(matrix_row_t local[], matrix_row_t remote[]) {
    return transport_master_if_connected(local, remote);
}
void transport_slave(matrix_row_t remote[], matrix_row_t local[]) {
    (void)remote;
    (void)local;
    ++split_calls;
}
void matrix_scan_user(void) {
    ++hooks;
}
void matrix_slave_scan_user(void) {
    ++hooks;
}
void lpwr_stop_prepare(void);
void palcallback(void *arg) {
    unsigned line = (uintptr_t)arg;
    // The existing STOP callback masks its entire vector after firing.
    vectors[line <= 4 ? line : line <= 9 ? EXTI9_5_IRQn : EXTI15_10_IRQn] = false;
}
static void foreign(void *arg) {
    (void)arg;
}
static void scans(unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        ++now;
        matrix_scan();
    }
}
static void setup(bool is_left, bool is_master) {
    left        = is_left;
    isLeftHand  = is_left;
    master      = is_master;
    power_state = LPWR_NORMAL;
    memset(down, 0, sizeof(down));
    matrix_init();
    memset(_pal_events, 0, sizeof(_pal_events));
    memset(EXTI, 0, sizeof(*EXTI));
    connected = true;
    memset(peer, 0, sizeof(peer));
    scans(20);
}
static void test_idle_and_wake(bool is_left, bool is_master) {
    setup(is_left, is_master);
    unsigned before_reads = reads, before_drives = drives, before_hooks = hooks, before_split = split_calls;
    scans(1000);
    assert(reads == before_reads && drives == before_drives);
    assert(hooks == before_hooks + 1000 && split_calls == before_split + 1000);
    if (master) {
        peer[2] = 4;
        scans(1);
        assert(matrix[thatHand + 2] == 4);
        connected = false;
        scans(1);
        assert(matrix[thatHand + 2] == 0);
    }
    down[4][is_left ? 7 : 8] = true;
    fire(4);
    scans(1);
    assert(reads > before_reads && matrix[thisHand + 4] == 0);
    scans(5);
    assert(matrix[thisHand + 4] == (1U << (is_left ? 7 : 8)));
    before_reads = reads;
    scans(20);
    assert(reads > before_reads); // Held input never arms idle.
    down[4][is_left ? 7 : 8] = false;
    scans(1);
    assert(matrix[thisHand + 4]);
    scans(5);
    assert(matrix[thisHand + 4] == 0);
    scans(10);
    before_reads = reads;
    scans(50);
    assert(reads == before_reads);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    bool is_left   = strcmp(argv[1], "left") == 0;
    bool is_master = strcmp(argv[2], "master") == 0;
    test_idle_and_wake(is_left, is_master);
    setup(is_left, is_master);
    matrix_init();
    edge_on_drive = true;
    scans(20);
    assert(matrix[thisHand] == 1); // A key already down during arming is not missed.
    down[0][0] = false;
    scans(20);
    matrix_init();
    pulse_on_unlock = true;
    scans(5);
    unsigned before = reads;
    scans(1);
    assert(reads > before); // A latched short edge still resumes native scanning.
    scans(20);
    matrix_init();
    EXTI->IMR = EXTI->RTSR = 1U << 2;
    _pal_events[2]         = (palevent_t){.cb = foreign};
    before                 = reads;
    scans(20);
    assert(reads > before && _pal_events[2].cb == foreign);
    EXTI->IMR = EXTI->RTSR = 0;
    _pal_events[2]         = (palevent_t){0};
    scans(20);
    chSysLock();
    lpwr_stop_prepare();
    chSysUnlock();
    assert(EXTI->IMR == 0); // Awake IRQ ownership yielded before STOP arms.
    // Native STOP retains its row wake callbacks after restoring clocks/pins.
    for (unsigned r = 0; r < 5; ++r) {
        unsigned line = PAL_PAD(rows[r]);
        EXTI->IMR |= 1U << line;
        EXTI->FTSR |= 1U << line;
        _pal_events[line].cb  = palcallback;
        _pal_events[line].arg = (void *)(uintptr_t)line;
    }
    EXTI->IMR |= 1U << 10;
    EXTI->FTSR |= 1U << 10;
    _pal_events[10] = (palevent_t){.cb = palcallback, .arg = (void *)(uintptr_t)10};
    EXTI->IMR |= 1U << 7;
    _pal_events[7].cb = foreign;
    power_state       = LPWR_WAKEUP;
    scans(1);
    power_state = LPWR_NORMAL;
    scans(20);
    before = reads;
    scans(20);
    assert(reads == before);
    assert((EXTI->IMR & (1U << 7)) && _pal_events[7].cb == foreign);
    fire_line(10); // Split RX must not mask C13's idle-wake vector.
    down[4][0] = true;
    fire(4);
    scans(6);
    assert(matrix[thisHand + 4] == 1);
    down[4][0] = false;
    scans(20);
    down[1][0] = true;
    fire(1);
    scans(1);
    down[1][0] = false; // A brief bounce never becomes a cooked keypress.
    scans(20);
    assert(matrix[thisHand + 1] == 0);
    now = UINT32_MAX - 3;
    matrix_init();
    scans(20);
    before = reads;
    scans(20);
    assert(reads == before); // Quiet-time arithmetic survives timer wrap.
    puts("Native matrix idle: both pinouts, split maintenance, debounce, arm races, IRQ conflict and STOP handoff passed");
}

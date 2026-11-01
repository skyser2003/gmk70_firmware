// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "lowpower.h"
#include "wls/wls.h"

#if !defined(SPLIT_KEYBOARD) || DIODE_DIRECTION != ROW2COL
#    error "GMK70 idle scanning requires the shared ROW2COL split matrix"
#endif

static const pin_t   rows[]        = MATRIX_ROW_PINS;
static const pin_t   left_cols[]   = MATRIX_COL_PINS;
static const pin_t   right_cols[]  = MATRIX_COL_PINS_RIGHT;
static const pin_t   stop_inputs[] = {HS_BAT_CABLE_PIN, SERIAL_USART_RX_PIN, LPWR_EXTRA_WAKE_PIN, HS_BT_DEF_PIN, HS_2G4_DEF_PIN};
static bool          idle;
static bool          stop_prepared;
static volatile bool pending;
static uint32_t      quiet_since;
extern matrix_row_t  raw_matrix[MATRIX_ROWS], matrix[MATRIX_ROWS];
extern uint8_t       thisHand;
bool                 matrix_post_scan(void);
uint8_t              __real_matrix_scan(void);
void                 __real_matrix_init(void);
void                 palcallback(void *arg);

static const pin_t *columns(void) {
    return is_keyboard_left() ? left_cols : right_cols;
}

static void key_edge(void *arg) {
    (void)arg;
    pending = true;
}

static void end_idle(void) {
    if (!idle) return;
    for (unsigned r = 0; r < sizeof(rows) / sizeof(*rows); ++r) {
        unsigned line = PAL_PAD(rows[r]);
        if (_pal_events[line].cb == key_edge) palDisableLineEventI(rows[r]);
    }
    const pin_t *cols = columns();
    for (unsigned c = 0; c < MATRIX_COLS; ++c)
        if (cols[c] != NO_PIN) gpio_set_pin_input_high(cols[c]);
    idle        = false;
    quiet_since = timer_read32();
}

static bool owned(void) {
    for (unsigned r = 0; r < sizeof(rows) / sizeof(*rows); ++r) {
        unsigned line = PAL_PAD(rows[r]);
        if (_pal_events[line].cb != key_edge || !(EXTI->IMR & (1U << line))) return false;
    }
    return true;
}

static void enable_vector(unsigned line) {
    static const uint8_t priorities[] = {WB32_IRQ_EXTI0_PRIORITY, WB32_IRQ_EXTI1_PRIORITY, WB32_IRQ_EXTI2_PRIORITY, WB32_IRQ_EXTI3_PRIORITY, WB32_IRQ_EXTI4_PRIORITY};
    if (line <= 4)
        nvicEnableVector(EXTI0_IRQn + line, priorities[line]);
    else if (line <= 9)
        nvicEnableVector(EXTI9_5_IRQn, WB32_IRQ_EXTI5_9_PRIORITY);
    else
        nvicEnableVector(EXTI15_10_IRQn, WB32_IRQ_EXTI10_15_PRIORITY);
}

static void begin_idle(void) {
    // PAL EXTI channels are shared by all GPIO ports: never steal an owner.
    for (unsigned r = 0; r < sizeof(rows) / sizeof(*rows); ++r) {
        unsigned line = PAL_PAD(rows[r]);
        if ((EXTI->IMR | EXTI->RTSR | EXTI->FTSR) & (1U << line)) return;
        if (_pal_events[line].cb && _pal_events[line].cb != palcallback) return;
    }
    pending = false;
    idle    = true;
    for (unsigned r = 0; r < sizeof(rows) / sizeof(*rows); ++r) {
        unsigned line         = PAL_PAD(rows[r]);
        _pal_events[line].cb  = key_edge;
        _pal_events[line].arg = NULL;
        palEnableLineEventI(rows[r], PAL_EVENT_MODE_FALLING_EDGE);
        enable_vector(line);
    }
    const pin_t *cols = columns();
    for (unsigned c = 0; c < MATRIX_COLS; ++c) {
        if (cols[c] == NO_PIN) continue;
        gpio_write_pin_low(cols[c]);
        gpio_set_pin_output_open_drain(cols[c]);
    }
    // Arm first, drive second, then sample: an already-held key needs no edge.
    waitInputPinDelay();
    for (unsigned r = 0; r < sizeof(rows) / sizeof(*rows); ++r)
        if (!gpio_read_pin(rows[r])) pending = true;
}

static void retire_stop_input(pin_t pin) {
    if (_pal_events[PAL_PAD(pin)].cb == palcallback) palDisableLineEventI(pin);
}

uint8_t __wrap_matrix_scan(void) {
    chSysLock();
    if (stop_prepared && lpwr_get_state() == LPWR_NORMAL) {
        // STOP callbacks mask entire IRQ groups. Retire dormant sources,
        // including split RX sharing C13's vector, before enabling idle IRQs.
        // Independently owned callbacks remain untouched.
        for (unsigned r = 0; r < sizeof(rows) / sizeof(*rows); ++r)
            retire_stop_input(rows[r]);
        for (unsigned i = 0; i < sizeof(stop_inputs) / sizeof(*stop_inputs); ++i)
            retire_stop_input(stop_inputs[i]);
        stop_prepared = false;
    }
    bool skip = idle && !pending && owned() && lpwr_get_state() == LPWR_NORMAL;
    if (!skip) end_idle();
    chSysUnlock();
    // Keep the native split poll, peer disconnect releases and board hooks.
    if (skip) return matrix_post_scan();
    uint8_t changed  = __real_matrix_scan();
    bool    released = true;
    for (unsigned r = 0; r < MATRIX_ROWS / 2; ++r)
        released &= raw_matrix[r] == 0 && matrix[thisHand + r] == 0;
    if (!released || lpwr_get_state() != LPWR_NORMAL) quiet_since = timer_read32();
    if (released && lpwr_get_state() == LPWR_NORMAL && timer_elapsed32(quiet_since) >= DEBOUNCE) {
        chSysLock();
        begin_idle();
        chSysUnlock();
    }
    return changed;
}

void __wrap_matrix_init(void) {
    chSysLock();
    end_idle();
    pending     = false;
    quiet_since = timer_read32();
    chSysUnlock();
    __real_matrix_init();
}

void lpwr_matrix_idle_stop(void) {
    // The existing STOP entry owns the kernel lock and will arm its wake pins.
    end_idle();
    stop_prepared = true;
}

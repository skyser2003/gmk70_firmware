// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "wireless.h"
#include "util.h"

#ifndef LPWR_UART_WAKEUP_DISABLE
#    include "uart.h"
#endif

// MATRIX_ROWS includes both halves; infer the physical row count from the pins.
static ioline_t       row_pins[]                 = MATRIX_ROW_PINS;
static const ioline_t left_col_pins[MATRIX_COLS] = MATRIX_COL_PINS;
#ifdef MATRIX_COL_PINS_RIGHT
static const ioline_t right_col_pins[MATRIX_COLS] = MATRIX_COL_PINS_RIGHT;
#endif

#if PAL_USE_CALLBACKS != TRUE
#    error PAL_USE_CALLBACKS must be set to TRUE!
#endif

#if !((DIODE_DIRECTION == ROW2COL) || (DIODE_DIRECTION == COL2ROW))
#    error DIODE_DIRECTION must be one of COL2ROW or ROW2COL!
#endif

// clang-format off
static const uint32_t pre_lp_code[] = {553863175u, 554459777u, 1208378049u, 4026624001u, 688390415u, 554227969u, 3204472833u, 1198571264u, 1073807360u, 1073808388u};
#define PRE_LP() ((void (*)(void))((uintptr_t)(pre_lp_code) | 0x01))()

static const uint32_t post_lp_code[] = {553863177u, 554459777u, 1208509121u, 51443856u, 4026550535u, 1745485839u, 3489677954u, 536895496u, 673389632u, 1198578684u, 1073807360u, 536866816u, 1073808388u};
#define POST_LP() ((void (*)(void))((uintptr_t)(post_lp_code) | 0x01))()
// clang-format on

extern void __early_init(void);
extern void matrix_init_pins(void);

void palcallback_cb(uint8_t line) __attribute__((weak));
void palcallback_cb(uint8_t line) {}

static volatile bool wake_event_serviced;

void palcallback(void *arg) {
    uint8_t line        = (uintptr_t)arg & 0xFF;
    wake_event_serviced = true;

    switch (line) {
#ifndef LPWR_UART_WAKEUP_DISABLE
        case PAL_PAD(UART_RX_PIN): {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_UART);
        } break;
#endif
        default: {
            lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_MATRIX);
        } break;
    }

    palcallback_cb(line);

    // V15 disables only the group that fired. Leave independent wake groups
    // enabled and preserve their pending edges until their handlers run.
    if (line <= 4) {
        nvicDisableVector(EXTI0_IRQn + line);
        EXTI->PR = 1u << line;
    } else if (line <= 9) {
        nvicDisableVector(EXTI9_5_IRQn);
        EXTI->PR = 0x3E0;
    } else {
        nvicDisableVector(EXTI15_10_IRQn);
        EXTI->PR = 0xFC00;
    }
}

void pal_events_init(void) {
    for (uint8_t i = 0; i < 16; i++) {
        _pal_events[i].cb  = palcallback;
        _pal_events[i].arg = (void *)(uintptr_t)i;
    }
}

void lpwr_exti_init_hook(void) __attribute__((weak));
void lpwr_exti_init_hook(void) {}

void lpwr_exti_init_post_hook(void) __attribute__((weak));
void lpwr_exti_init_post_hook(void) {}

bool lpwr_matrix_wake_allowed_hook(void) __attribute__((weak));
bool lpwr_matrix_wake_allowed_hook(void) {
    return true;
}

bool lpwr_matrix_pin_wake_allowed_hook(uint32_t pin) __attribute__((weak));
bool lpwr_matrix_pin_wake_allowed_hook(uint32_t pin) {
    return true;
}

void lpwr_enable_wake_line(uint32_t pin, uint32_t edges) {
    uint8_t line = PAL_PAD(pin);
    // A prior STOP may leave this PAL event configured. Reconfigure only this
    // line, including its callback, and enable only its stock EXTI group.
    // The caller holds the kernel lock; I-class calls preserve it while arming.
    palDisableLineEventI(pin);
    _pal_events[line].cb  = palcallback;
    _pal_events[line].arg = (void *)(uintptr_t)line;
    palEnableLineEventI(pin, edges);
    if (line <= 4) {
        static const uint8_t priority[] = {WB32_IRQ_EXTI0_PRIORITY, WB32_IRQ_EXTI1_PRIORITY, WB32_IRQ_EXTI2_PRIORITY, WB32_IRQ_EXTI3_PRIORITY, WB32_IRQ_EXTI4_PRIORITY};
        nvicEnableVector(EXTI0_IRQn + line, priority[line]);
    } else if (line <= 9) {
        nvicEnableVector(EXTI9_5_IRQn, WB32_IRQ_EXTI5_9_PRIORITY);
    } else {
        nvicEnableVector(EXTI15_10_IRQn, WB32_IRQ_EXTI10_15_PRIORITY);
    }
}

void lpwr_exti_init(void) {
    pal_events_init();
    lpwr_exti_init_hook();
    const ioline_t *col_pins = left_col_pins;
#ifdef MATRIX_COL_PINS_RIGHT
    // V15 uses its master predicate (USB present OR left-side signal).
    if (!is_keyboard_master()) col_pins = right_col_pins;
#endif

    bool matrix_wake = lpwr_matrix_wake_allowed_hook();
#if DIODE_DIRECTION == ROW2COL
    for (uint8_t i = 0; i < MATRIX_COLS; i++) {
        if (col_pins[i] == NO_PIN) continue;
        if (matrix_wake && lpwr_matrix_pin_wake_allowed_hook(left_col_pins[i])) {
            gpio_set_pin_output_open_drain(col_pins[i]);
            gpio_write_pin_low(col_pins[i]);
        } else {
            gpio_set_pin_input_high(col_pins[i]);
        }
    }
    for (uint8_t i = 0; i < ARRAY_SIZE(row_pins); i++) {
        if (row_pins[i] == NO_PIN) continue;
        if (matrix_wake && lpwr_matrix_pin_wake_allowed_hook(row_pins[i])) {
            gpio_set_pin_input_high(row_pins[i]);
            waitInputPinDelay();
            lpwr_enable_wake_line(row_pins[i], PAL_EVENT_MODE_BOTH_EDGES);
        } else {
            gpio_set_pin_input_low(row_pins[i]);
        }
    }
#elif DIODE_DIRECTION == COL2ROW
    for (uint8_t i = 0; i < ARRAY_SIZE(row_pins); i++) {
        if (row_pins[i] == NO_PIN) continue;
        if (matrix_wake && lpwr_matrix_pin_wake_allowed_hook(row_pins[i])) {
            gpio_set_pin_output_open_drain(row_pins[i]);
            gpio_write_pin_low(row_pins[i]);
        } else {
            gpio_set_pin_input_high(row_pins[i]);
        }
    }
    for (uint8_t i = 0; i < MATRIX_COLS; i++) {
        if (col_pins[i] == NO_PIN) continue;
        if (matrix_wake && lpwr_matrix_pin_wake_allowed_hook(left_col_pins[i])) {
            gpio_set_pin_input_high(col_pins[i]);
            waitInputPinDelay();
            lpwr_enable_wake_line(col_pins[i], PAL_EVENT_MODE_BOTH_EDGES);
        } else {
            gpio_set_pin_input_low(col_pins[i]);
        }
    }
#endif

#ifndef LPWR_UART_WAKEUP_DISABLE
    gpio_set_pin_input(UART_RX_PIN);
    waitInputPinDelay();
    lpwr_enable_wake_line(UART_RX_PIN, PAL_EVENT_MODE_BOTH_EDGES);
#endif

    lpwr_exti_init_post_hook();
}

void lpwr_clock_enable_user(void) __attribute__((weak));
void lpwr_clock_enable_user(void) {}

void lpwr_clock_enable(void) {
    __early_init();

    rccEnableEXTI();

#if WB32_SERIAL_USE_UART1
    rccEnableUART1();
#endif
#if WB32_SERIAL_USE_UART2
    rccEnableUART2();
#endif
#if WB32_SERIAL_USE_UART3
    rccEnableUART3();
#endif
#if WB32_SPI_USE_QSPI
    rccEnableQSPI();
#endif
#if WB32_SPI_USE_SPIM2
    rccEnableSPIM2();
#endif
#if WB32_I2C_USE_I2C1
    rccEnableI2C1();
#endif
#if WB32_I2C_USE_I2C2
    rccEnableI2C2();
#endif

#ifndef LPWR_UART_WAKEUP_DISABLE
    palSetLineMode(UART_RX_PIN, PAL_MODE_ALTERNATE(UART_RX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
#endif

    lpwr_clock_enable_user();
}

void lpwr_stop_prepare(void) {
    // Called under the kernel lock BEFORE lpwr_exti_init arms the wake sources.
    // Clearing these after arming can discard a real matrix/split wake edge.
    EXTI->PR            = 0x7FFFF;
    wake_event_serviced = false;
    for (uint8_t i = 0; i < 8; i++) {
        for (uint8_t j = 0; j < 32; j++) {
            if (NVIC->ISPR[i] & (0x01UL << j)) {
                NVIC->ICPR[i] = (0x01UL << j);
            }
        }
    }
}

bool lpwr_stop_pending(void) {
    // A callback may run between arming and CPU masking. Do not sleep again
    // after servicing it, including an ignored wake. A prior UART cause alone
    // is not an accepted wake event.
    lpwr_wakeupcd_t cause = lpwr_get_sleep_wakeupcd();
    return wake_event_serviced || (cause != LPWR_WAKEUP_NONE && cause != LPWR_WAKEUP_UART);
}

void wb32_stop_mode(void) {
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;
    // Keep EXTI/NVIC wake requests that arrived since arming. With PRIMASK set,
    // a pending enabled IRQ still releases WFI and runs after chSysEnable.

    /* Clear all bits except DBP and FCLKSD bit */
    PWR->CR0 &= 0x09U;

    // STOP LP4 MODE S32KON
    PWR->CR0 |= 0x3B004U;
    PWR->CFGR = 0x3B3;

    PRE_LP();

    /* Set SLEEPDEEP bit of Cortex System Control Register */
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    /* Request Wait For Interrupt */
    __WFI();

    POST_LP();

    /* Clear SLEEPDEEP bit of Cortex System Control Register */
    SCB->SCR &= (~SCB_SCR_SLEEPDEEP_Msk);

    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
}

void mcu_stop_mode(void) {
    wb32_stop_mode();
}

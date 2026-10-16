// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "host_driver.h"

typedef uint32_t ioline_t;
void             gpio_set_pin_input_low(ioline_t pin);
bool             gmk70_stock_cable_flag(void);
enum { A1 = 1, A2, A3, A4, A5, A6, A7, A8, A9, A10, B0 = 16, B6 = 22, B10 = 26, B11, B12, B13, B14, C0 = 32, C1, C2, C3, C4, C5, C6, C7, C8, C9, C11 = 43, C13 = 45, C14, C15 };
#define NO_PIN UINT32_MAX
#define MATRIX_COLS 9
#define MATRIX_ROW_PINS {A1, A2, A3, A4, C13}
#define MATRIX_COL_PINS {C0, C1, C2, C3, A6, B10, B11, B12, NO_PIN}
#define MATRIX_COL_PINS_RIGHT {B13, B14, C6, C7, C8, C9, C4, C5, B0}
#define ROW2COL 1
#define COL2ROW 2
#define DIODE_DIRECTION ROW2COL
#define TRUE 1
#define PAL_USE_CALLBACKS TRUE
#define LPWR_UART_WAKEUP_DISABLE
#define LPWR_EXTRA_WAKE_PIN B6
#define SERIAL_USART_TX_PIN A9
#define SERIAL_USART_RX_PIN A10
#define HS_BAT_CABLE_PIN A7
#define LED_POWER_EN_PIN A5
#define LED_POWER_EN2_PIN A8
#define PAL_PAD(pin) ((pin) & 15)
#define PAL_EVENT_MODE_RISING_EDGE 1
#define PAL_EVENT_MODE_BOTH_EDGES 3
#define EXTI0_IRQn 6
#define EXTI9_5_IRQn 16
#define EXTI15_10_IRQn 33
#define WB32_IRQ_EXTI0_PRIORITY 6
#define WB32_IRQ_EXTI1_PRIORITY 6
#define WB32_IRQ_EXTI2_PRIORITY 6
#define WB32_IRQ_EXTI3_PRIORITY 6
#define WB32_IRQ_EXTI4_PRIORITY 6
#define WB32_IRQ_EXTI5_9_PRIORITY 6
#define WB32_IRQ_EXTI10_15_PRIORITY 6
#define SysTick_CTRL_ENABLE_Msk 1
#define SCB_ICSR_PENDSTCLR_Msk (1u << 25)
#define SCB_SCR_SLEEPDEEP_Msk 4
typedef struct {
    void (*cb)(void *);
    void *arg;
} test_event_t;
extern test_event_t _pal_events[16];
typedef struct {
    uint32_t PR;
} test_exti_t;
typedef struct {
    uint32_t ISPR[8], ICPR[8];
} test_nvic_t;
typedef struct {
    uint32_t CTRL;
} test_systick_t;
typedef struct {
    uint32_t ICSR, SCR;
} test_scb_t;
typedef struct {
    uint32_t CR0, CFGR;
} test_pwr_t;
extern test_exti_t    test_exti;
extern test_nvic_t    test_nvic;
extern test_systick_t test_systick;
extern test_scb_t     test_scb;
extern test_pwr_t     test_pwr;
#define EXTI (&test_exti)
#define NVIC (&test_nvic)
#define SysTick (&test_systick)
#define SCB (&test_scb)
#define PWR (&test_pwr)
static inline void rccEnableEXTI(void) {}
static inline void __WFI(void) {}
void               palEnableLineEventI(ioline_t pin, uint32_t edges);
void               palDisableLineEventI(ioline_t pin);
void               nvicEnableVector(unsigned vector, unsigned priority);
void               nvicDisableVector(unsigned vector);
void               gpio_set_pin_output_open_drain(ioline_t pin);
void               gpio_set_pin_output(ioline_t pin);
void               gpio_set_pin_input_high(ioline_t pin);
void               gpio_set_pin_input(ioline_t pin);
void               gpio_write_pin_low(ioline_t pin);
bool               gpio_read_pin(ioline_t pin);
void               waitInputPinDelay(void);
void               wait_ms(unsigned ms);
void               wait_us(uint16_t us);
void               suspend_wakeup_init_user(void);
bool               is_keyboard_master(void);
uint32_t           timer_read32(void);
uint32_t           timer_elapsed32(uint32_t timestamp);
void               suspend_wakeup_init(void);
void               suspend_wakeup_init_quantum(void);
void               gmk70_stock_usb_sleep_set(bool requested);

void chSysLock(void);
void chSysUnlock(void);
void chSysDisable(void);
void chSysEnable(void);
void suspend_power_down(void);
bool rgb_matrix_is_enabled(void);
void rgb_matrix_disable_noeeprom(void);
void rgb_matrix_enable_noeeprom(void);

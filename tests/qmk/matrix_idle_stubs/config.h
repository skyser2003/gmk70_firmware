// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define MATRIX_ROWS 10
#define MATRIX_COLS 9
#define SPLIT_KEYBOARD
#define DIODE_DIRECTION 1
#define DEBOUNCE 5
#define HS_BAT_CABLE_PIN 7
#define SERIAL_USART_RX_PIN 10
#define LPWR_EXTRA_WAKE_PIN 22
#define C14 46
#define C15 47
#define MATRIX_ROW_PINS {1, 2, 3, 4, 45}
#define MATRIX_COL_PINS {32, 33, 34, 35, 6, 26, 27, 28, NO_PIN}
#define MATRIX_COL_PINS_RIGHT {29, 30, 38, 39, 40, 41, 36, 37, 16}
#define NO_PIN UINT16_MAX
#define WB32_IRQ_EXTI0_PRIORITY 6
#define WB32_IRQ_EXTI1_PRIORITY 6
#define WB32_IRQ_EXTI2_PRIORITY 6
#define WB32_IRQ_EXTI3_PRIORITY 6
#define WB32_IRQ_EXTI4_PRIORITY 6
#define WB32_IRQ_EXTI5_9_PRIORITY 6
#define WB32_IRQ_EXTI10_15_PRIORITY 6
#define EXTI0_IRQn 0
#define EXTI9_5_IRQn 5
#define EXTI15_10_IRQn 6
bool is_keyboard_left(void);
bool is_keyboard_master(void);

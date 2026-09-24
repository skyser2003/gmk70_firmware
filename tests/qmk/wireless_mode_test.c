// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include "quantum.h"

enum { C14 = 14, C15 = 15 };
static uint16_t gpio_c_input;

static bool gpio_read_pin(uint8_t pin) {
    assert(pin == C14 || pin == C15);
    return (gpio_c_input & (1u << pin)) != 0;
}

#define QMK_KEYBOARD_H "quantum.h"
#include "wls.h"

typedef struct {
    uint16_t         input;
    enum modeio_mode expected;
} mode_case_t;

int main(void) {
    // Truth table recovered from stock V13 at 0x08002404; see docs/STOCK-MODE-SWITCH.md.
    const mode_case_t cases[] = {
        {(1u << C14) | (1u << C15), hs_usb},
        {1u << C14, hs_bt},
        {1u << C15, hs_2g4},
        {0, hs_none},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        gpio_c_input = cases[index].input;
        if (HS_GET_MODE_PIN(HS_USB_PIN_STATE) != (cases[index].expected == hs_usb) || HS_GET_MODE_PIN(HS_BT_PIN_STATE) != (cases[index].expected == hs_bt) || HS_GET_MODE_PIN(HS_2G4_PIN_STATE) != (cases[index].expected == hs_2g4)) {
            fprintf(stderr, "Mode selector mismatch for GPIOC input 0x%04x\n", gpio_c_input);
            return 1;
        }
    }
    puts("GMK70 mode selector matches stock V13 for all four pin states");
    return 0;
}

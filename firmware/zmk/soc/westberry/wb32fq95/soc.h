/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GMK70_WB32FQ95_SOC_H
#define GMK70_WB32FQ95_SOC_H

#include <cmsis_core_m_defaults.h>

#ifndef _ASMLANGUAGE
#    include <stdint.h>

void wb32_usb_clock_enable(void);
void wb32_usb_disconnect(void);
void wb32_usb_clock_disable(void);

/* Register layout and constants: Westberry CMSIS WB32FQ95xx, pinned in
 * dependencies/sources.lock.json qmk.libraries.lib/chibios-contrib.
 * See firmware/zmk/PROVENANCE.md. */
#    define WB32_RCC_BASE 0x40010c00U
#    define WB32_GPIOA_BASE 0x40000000U
#    define WB32_GPIOB_BASE 0x40000400U
#    define WB32_GPIOC_BASE 0x40000800U
#    define WB32_GPIOD_BASE 0x40000c00U

struct wb32_gpio {
    volatile uint32_t moder;
    volatile uint32_t otyper;
    volatile uint32_t ospeedr;
    volatile uint32_t pupdr;
    volatile uint32_t idr;
    volatile uint32_t odr;
    volatile uint32_t bsrr;
    volatile uint32_t lckr;
    volatile uint32_t afrl;
    volatile uint32_t afrh;
    volatile uint32_t smit;
    volatile uint32_t current;
    volatile uint32_t cfgmsk;
};
#endif
#endif

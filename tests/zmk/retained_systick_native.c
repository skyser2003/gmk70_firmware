/* SPDX-License-Identifier: Apache-2.0 */
/* Only the ARM IRQ calling-convention annotation is inapplicable on the host.
 * The pinned driver's executable timer implementation is compiled directly. */
#define __attribute__(...)
#include "../../dependencies/zephyr/drivers/timer/cortex_m_systick.c"
#undef __attribute__
void retained_test_systick_init(void) {
    sys_clock_driver_init();
}

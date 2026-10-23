/* SPDX-License-Identifier: GPL-2.0-or-later
 * RAM-only one-shot link reconnect; captured stock GAP API, no reset/pairing. */
#include <stdint.h>
#ifndef GMK70_TRIAL_STATUS_ADDRESS
#    error "Supply a status address inside the verified owned allocation"
#endif
void radio_reconnect(void) {
    uint8_t connection = *(volatile uint8_t *)0x11003CD8u;
    if (connection >= 8) return;
    uint32_t previous;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(previous)::"memory");
    if (((uint32_t (*)(uint32_t, uint32_t))0xA64Du)(256, 3)) {
        *(volatile uint32_t *)GMK70_TRIAL_STATUS_ADDRESS = 0x5245434Eu;
        ((void (*)(uint8_t))0x10007A0Du)(connection);
    }
    __asm volatile("msr primask, %0" ::"r"(previous) : "memory");
}

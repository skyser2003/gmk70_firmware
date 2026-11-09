/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Before Zephyr initializes RAM, clocks or interrupts. This replaces only the
 * resident's factory-DFU magic check in direct-boot images, never its installer.
 * ROM address/magic match pinned QMK platforms/chibios/bootloaders/wb32_dfu.c.
 */
.syntax unified
.thumb
.section .text.soc_reset_hook,"ax",%progbits
.global soc_reset_hook
.type soc_reset_hook,%function
.thumb_func
soc_reset_hook:
    ldr r0, =0x20006ffc
    ldr r1, [r0]
    ldr r2, =0xdeadbeef
    cmp r1, r2
    bne 1f
    movs r1, #0
    str r1, [r0]
    cpsid i
    msr control, r1
    msr basepri, r1
    msr faultmask, r1
    ldr r0, =0x1fffe000
    ldr r2, =0xe000ed08
    str r0, [r2]
    dsb
    isb
    ldr r1, [r0, #4]
    ldr r2, [r0]
    msr msp, r2
    cpsie i
    bx r1
1:
    bx lr
.size soc_reset_hook, .-soc_reset_hook

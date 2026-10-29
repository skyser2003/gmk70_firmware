/* SPDX-License-Identifier: GPL-2.0-or-later */
.syntax unified
.cpu cortex-m3
.thumb
.section .text.gmk_zmk_resident_branch,"ax",%progbits
.global gmk_zmk_resident_branch
.type gmk_zmk_resident_branch,%function
.thumb_func
gmk_zmk_resident_branch:
    cpsid i
    ldr r1, [r0]
    ldr r2, [r0, #4]
    ldr r3, [r0, #12]
    mov lr, r3
    ldr r3, =0xe000ed08
    str r0, [r3]
    movs r0, #0
    msr psp, r0
    msr basepri, r0
    msr faultmask, r0
    msr control, r0
    dsb
    isb
    msr msp, r1
    dsb
    isb
    bx r2
.size gmk_zmk_resident_branch, .-gmk_zmk_resident_branch

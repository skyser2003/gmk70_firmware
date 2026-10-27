/* SPDX-License-Identifier: GPL-2.0-or-later */
.syntax unified
.cpu cortex-m3
.thumb

.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.type Reset_Handler,%function
.thumb_func
Reset_Handler:
    cpsid i
    ldr r0, =__main_stack_end__
    msr msp, r0
    movs r0, #0
    msr control, r0
    dsb
    isb
    bl gmk_trial_early_boot
    b _crt0_entry
.size Reset_Handler, .-Reset_Handler

/* r0 is the vector address. No C stack access is permitted after this point. */
.section .text.gmk_trial_platform_handoff,"ax",%progbits
.global gmk_trial_platform_handoff
.type gmk_trial_platform_handoff,%function
.thumb_func
gmk_trial_platform_handoff:
    cpsid i
    ldr r1, [r0]
    ldr r2, [r0, #4]
    ldr r3, =0xE000ED08
    str r0, [r3]
    movs r0, #0
    msr psp, r0
    msr basepri, r0
    msr faultmask, r0
    msr control, r0
    dsb
    isb
    msr msp, r1
    ldr lr, =gmk_trial_return_reset
    dsb
    isb
    /* Trial startup owns enabling IRQs once its own drivers are ready. */
    bx r2
.size gmk_trial_platform_handoff, .-gmk_trial_platform_handoff

/* A trial that returns through LR gets a real reset, never a C epilogue. */
.section .text.gmk_trial_return_reset,"ax",%progbits
.type gmk_trial_return_reset,%function
.thumb_func
gmk_trial_return_reset:
    cpsid i
    ldr r0, =0xE000ED0C
    ldr r1, =0x05FA0004
    dsb
    str r1, [r0]
    dsb
1:  b 1b
.size gmk_trial_return_reset, .-gmk_trial_return_reset

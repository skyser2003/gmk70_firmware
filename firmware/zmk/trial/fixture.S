/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Bare Cortex-M3 proof fixtures, NOT Zephyr/ZMK firmware. Never refresh IWDG. */
.syntax unified
.cpu cortex-m3
.thumb
.section .vectors,"a",%progbits
.word 0x20006FC0
.word trial_reset, trial_fault, trial_fault, trial_fault, trial_fault, trial_fault
.word 0, 0, 0, 0
.word trial_fault, trial_fault, 0, trial_fault, trial_fault
.section .text,"ax",%progbits
.global trial_reset
.type trial_reset,%function
.thumb_func
trial_reset:
    cpsid i
#ifdef GMK_TRIAL_RETURN
    b trial_fault
#else
1:  b 1b
#endif
.size trial_reset, .-trial_reset
.type trial_fault,%function
.thumb_func
trial_fault:
    ldr r0, =0xE000ED0C
    ldr r1, =0x05FA0004
    dsb
    str r1, [r0]
    dsb
2:  b 2b
.size trial_fault, .-trial_fault

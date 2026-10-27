/* SPDX-License-Identifier: GPL-2.0-or-later */
.syntax unified
.cpu cortex-m3
.thumb

/* Ordinary faults retain the upstream handler. Only an explicitly entered
 * runtime probe may capture a frame and request a software reset. */
.macro PROBE_FAULT name, kind
.section .text.\name,"ax",%progbits
.global \name
.type \name,%function
.thumb_func
\name:
    ldr r3, =gmk_trial_probe_active
    ldr r3, [r3]
    ldr r12, =0x47545052
    cmp r3, r12
    bne _unhandled_exception
    /* A reset preserves SRAM until CRT. Its RCC flags revoke a stale marker
     * before any startup fault can be mistaken for the previous probe. */
    ldr r3, =0x40010D00
    ldr r3, [r3]
    cmp r3, #0
    bne _unhandled_exception
    cpsid i
    tst lr, #4
    ite eq
    mrseq r2, msp
    mrsne r2, psp
    movs r0, #\kind
    movs r1, #0
    ldr r3, =0x20000040
    cmp r2, r3
    blo 1f
    ldr r3, =0x20006FE0
    cmp r2, r3
    bhi 1f
    tst r2, #3
    bne 1f
    ldr r1, [r2, #24]
1:
    /* The frame may use PSP. Recover C on the normal exception stack without
     * returning through the failed instruction or reading an unchecked SP. */
    ldr r3, =__main_stack_end__
    msr msp, r3
    b gmk_trial_runtime_fault
.size \name, .-\name
.endm

PROBE_FAULT HardFault_Handler, 1
PROBE_FAULT NMI_Handler, 2
PROBE_FAULT MemManage_Handler, 3
PROBE_FAULT BusFault_Handler, 4
PROBE_FAULT UsageFault_Handler, 5

.section .text.gmk_trial_runtime_trigger_fault,"ax",%progbits
.global gmk_trial_runtime_trigger_fault
.type gmk_trial_runtime_trigger_fault,%function
.thumb_func
gmk_trial_runtime_trigger_fault:
    udf #0
    bx lr
.size gmk_trial_runtime_trigger_fault, .-gmk_trial_runtime_trigger_fault

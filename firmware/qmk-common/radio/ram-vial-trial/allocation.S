/* SPDX-License-Identifier: GPL-2.0-or-later
 * OFFLINE RAM-only experiment, deliberately unarmed. No installer. */
.syntax unified
.cpu cortex-m3
.thumb
.section .crc_false,"ax",%progbits
.global crc_false
.thumb_func
crc_false:
    movs r0, #0
    bx lr

.section .once,"ax",%progbits
.thumb_func
once:
    push {r4-r7, lr}
    sub sp, #4
    mrs r4, primask
    cpsid i
    ldr r5, =0x11001843
    movs r0, #0xe0
    strb r0, [r5]
    dsb sy
    isb sy
    ldr r5, =mailbox
    ldr r0, [r5]
    cmp r0, #1
    bne done
    movs r0, #0
    str r0, [r5]
    mrs r0, ipsr
    str r0, [r5, #12]
    cmp r0, #0
    bne handler_mode
    ldr r6, [r5, #16]
    cmp.w r6, #2048
    bne handler_mode
    mov r0, r6
    movs r1, #3
    ldr r7, =0x0000a64d
    blx r7
    cbz r0, no_space
    mov r0, r6
    ldr r7, =0x10011ddd
    blx r7
    str r0, [r5, #4]
    cmp r0, #0
    ite ne
    movne r0, #1
    moveq r0, #2
    b status
handler_mode:
    movs r0, #3
    b status
no_space:
    movs r0, #4
status:
    str r0, [r5, #8]
done:
    msr primask, r4
    add sp, #4
    pop {r4-r7, pc}
    .ltorg

.section .gateway,"ax",%progbits
.global gateway
.thumb_func
gateway:
    push.w {r0-r3, ip, lr}
    bl once
    pop.w {r0-r3, ip, lr}
    b.w 0x1100196a

.section .mailbox,"aw",%progbits
.balign 4
.global mailbox
mailbox:
    .word 0, 0, 0, 0, 0

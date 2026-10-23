/* SPDX-License-Identifier: GPL-2.0-or-later
 * RAM-only experimental gateway. No device transport or flash operation. */
.syntax unified
.cpu cortex-m3
.thumb
.section .crc_false,"ax",%progbits
.global crc_false
.thumb_func
crc_false:
    movs r0, #0
    bx lr
.section .gateway,"ax",%progbits
.global gateway
.thumb_func
gateway:
    push.w {r0-r3, ip, lr}
#if ONE_SHOT
    ldr r0, =0x11001843
    movs r1, #0xe0
    strb r1, [r0]
    dsb sy
    isb sy
#endif
    mov r0, r4
    ldr ip, =__trial_target
    blx ip
    pop.w {r0-r3, ip, lr}
    b.w 0x1100196a
    .ltorg

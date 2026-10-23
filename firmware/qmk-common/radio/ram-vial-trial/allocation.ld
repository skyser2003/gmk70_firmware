/* SPDX-License-Identifier: GPL-2.0-or-later */
ENTRY(gateway)
SECTIONS {
    . = 0x11001AF4;
    .crc_false : { KEEP(*(.crc_false)) }
    .once : { *(.once) }
    ASSERT(. <= 0x11001B6A, "One-shot overlaps fixed gateway")
    . = 0x11001B6A;
    .gateway : { *(.gateway) }
    . = ALIGN(4);
    .mailbox : { *(.mailbox) }
    ASSERT(mailbox == 0x11001B7C, "Mailbox ABI changed")
    ASSERT(. <= 0x11001BA6, "One-shot exceeds exclusive CRC slot")
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}

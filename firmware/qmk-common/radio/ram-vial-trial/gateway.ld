/* SPDX-License-Identifier: GPL-2.0-or-later */
ENTRY(gateway)
SECTIONS {
    . = 0x11001AF4;
    .crc_false : { KEEP(*(.crc_false)) }
    . = 0x11001B6A;
    .gateway : { *(.gateway) }
    ASSERT(. <= 0x11001BA6, "Gateway exceeds exclusive CRC validator")
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}

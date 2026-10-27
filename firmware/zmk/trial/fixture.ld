/* SPDX-License-Identifier: GPL-2.0-or-later */
ENTRY(trial_reset)
MEMORY {
    trial (rx) : ORIGIN = 0x08020000, LENGTH = 120K
    ram (rwx) : ORIGIN = 0x20000040, LENGTH = 0x6F80
}
SECTIONS {
    .vectors : { KEEP(*(.vectors)) } > trial
    .text : { *(.text*) *(.rodata*) } > trial
    .padding : { BYTE(0xFF); . = ALIGN(1024); } > trial = 0xFF
    .data : { *(.data*) } > ram AT > trial
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > ram
    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) }
}
ASSERT(SIZEOF(.vectors) == 64, "Fixture core vector table must be 64 bytes")
ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "Fixture has no RAM startup")
ASSERT(ORIGIN(ram) + LENGTH(ram) == 0x20006FC0, "Keep reserved/boot magic RAM free")

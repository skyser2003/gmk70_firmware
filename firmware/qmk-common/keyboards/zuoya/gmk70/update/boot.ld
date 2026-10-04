/* SPDX-License-Identifier: GPL-2.0-or-later */
ENTRY(gmk_boot_reset)
MEMORY { boot (rx) : ORIGIN = 0x08000000, LENGTH = 8K }
SECTIONS {
    .vectors : { KEEP(*(.boot_vectors)) } > boot
    .text : { *(.text*) *(.rodata*) *(.glue*) } > boot
    .ARM.exidx : { *(.ARM.exidx*) } > boot
    .data : { *(.data*) } > boot
    .bss : { *(.bss*) *(COMMON) } > boot
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.data) == 0, "Resident installer must not use initialized RAM")
ASSERT(SIZEOF(.bss) == 0, "Resident installer must not use uninitialized RAM")

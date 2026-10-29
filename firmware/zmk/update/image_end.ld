/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Includes the initialized RAM load image, not merely the .text extent. */
#ifdef CONFIG_GMK70_RESIDENT_RECEIVER
/* After all initialized RAM load sections. The experimental configuration
 * disables Zephyr's optional last-section word; image checks require this
 * entire versioned capability footer to end at __gmk_image_end. */
SECTION_PROLOGUE(.gmk_boot_footer,,SUBALIGN(4))
{
    . = ALIGN(4);
    LONG(0x374b4d47) /* GMK7 */
    LONG(0x4b4d5a30) /* 0ZMK */
    LONG(0x544f4f42) /* BOOT */
    LONG(1)
} GROUP_LINK_IN(ROMABLE_REGION)
#endif
__gmk_image_end = __rom_region_start + _flash_used;

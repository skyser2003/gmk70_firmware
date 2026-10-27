/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Includes the initialized RAM load image, not merely the .text extent. */
__gmk_image_end = __rom_region_start + _flash_used;

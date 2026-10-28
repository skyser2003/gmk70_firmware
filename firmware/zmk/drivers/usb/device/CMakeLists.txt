# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_USB_DC_WB32 usb_dc_wb32.c)
zephyr_library_sources_ifdef(CONFIG_BOARD_GMK70_WIRED usb_startup_wb32.c)

# SPDX-License-Identifier: GPL-2.0-or-later
if(CONFIG_GMK70_NKRO)
  zephyr_library()
  zephyr_library_include_directories(. ${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(nkro.c behavior.c)
  zephyr_ld_options(-Wl,--wrap=zmk_hid_get_keyboard_report)
  zephyr_ld_options(-Wl,--wrap=zmk_hid_get_boot_report)
endif()

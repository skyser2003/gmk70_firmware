# SPDX-License-Identifier: GPL-2.0-or-later
if(CONFIG_GMK70_FUNCTION_FLIP)
  zephyr_library()
  zephyr_library_include_directories(. ../update
    ../../qmk-common/keyboards/zuoya/gmk70/update ${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(core.c flip.c behavior.c)
  zephyr_ld_options(-Wl,--wrap=raise_zmk_keycode_state_changed)
endif()

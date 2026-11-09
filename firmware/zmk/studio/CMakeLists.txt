# SPDX-License-Identifier: Apache-2.0
if(CONFIG_GMK70_ZMK_STUDIO_SETTINGS)
  zephyr_library()
  zephyr_library_include_directories(. ../update ../wired ../../qmk-common/keyboards/zuoya/gmk70/update ${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(settings_store.c settings_backend.c)
  # Pinned ZMK settings use sprintf without stdio.h; leave upstream unchanged.
  zephyr_compile_options("$<$<COMPILE_LANGUAGE:C>:-include>" "$<$<COMPILE_LANGUAGE:C>:stdio.h>")
  zephyr_ld_options(-Wl,--wrap=zmk_studio_core_get_lock_state)
endif()

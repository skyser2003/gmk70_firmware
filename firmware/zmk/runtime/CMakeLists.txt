# SPDX-License-Identifier: GPL-2.0-or-later
if(CONFIG_GMK70_ZMK_UPDATER OR CONFIG_GMK70_ZMK_STUDIO_SETTINGS)
  set(gmk_common ${CMAKE_CURRENT_LIST_DIR}/../../qmk-common/keyboards/zuoya/gmk70/update)
  zephyr_library()
  zephyr_library_include_directories(. ../wired ../update ${gmk_common} ${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(service.c ../update/wb32_flash.c ${gmk_common}/image.c)
  zephyr_library_sources_ifdef(CONFIG_GMK70_ZMK_STUDIO_SETTINGS storage.c)
endif()

# SPDX-License-Identifier: GPL-2.0-or-later
if(CONFIG_GMK70_ZMK_UPDATER)
  set(gmk_update_common ${CMAKE_CURRENT_LIST_DIR}/../../qmk-common/keyboards/zuoya/gmk70/update)
  zephyr_library()
  zephyr_library_include_directories(. ../wired ${gmk_update_common} ${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(receiver.c raw_hid.c service.c wb32_flash.c wb32_watchdog.c)
  # Compile the authoritative sources directly; no copied/generated source tree.
  zephyr_library_sources(${gmk_update_common}/image.c ${gmk_update_common}/protocol.c)
  if(CONFIG_GMK70_ZMK_PEER_TRIAL_RELAY)
    # Link only the common packet validator; no local trial backend is invoked.
    zephyr_library_sources(${gmk_update_common}/trial.c)
  endif()
endif()

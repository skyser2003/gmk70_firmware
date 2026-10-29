# SPDX-License-Identifier: GPL-2.0-or-later
if(CONFIG_GMK70_ZMK_UPDATER)
  set(gmk_update_common ${CMAKE_CURRENT_LIST_DIR}/../../qmk-common/keyboards/zuoya/gmk70/update)
  zephyr_library()
  zephyr_library_include_directories(. ../wired ${gmk_update_common} ${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(receiver.c service.c wb32_flash.c)
  if(CONFIG_GMK70_ZMK_STUDIO_SETTINGS)
    zephyr_library_sources(studio_feature.c)
    zephyr_ld_options(-Wl,--wrap=usb_hid_register_device)
    # Pinned ZMK behavior/keymap settings use sprintf without stdio.h.
    # Keep upstream immutable; add its missing standard declaration here.
    zephyr_compile_options("$<$<COMPILE_LANGUAGE:C>:-include>" "$<$<COMPILE_LANGUAGE:C>:stdio.h>")
  else()
    zephyr_library_sources(raw_hid.c)
  endif()
  # Compile the authoritative sources directly; no copied/generated source tree.
  zephyr_library_sources(${gmk_update_common}/image.c ${gmk_update_common}/protocol.c)
  if(CONFIG_GMK70_ZMK_PEER_TRIAL_RELAY)
    # Link only the common packet validator; no local trial backend is invoked.
    zephyr_library_sources(${gmk_update_common}/trial.c)
  endif()
endif()

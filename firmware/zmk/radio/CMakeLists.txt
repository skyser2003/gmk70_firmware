# SPDX-License-Identifier: GPL-2.0-or-later
if(CONFIG_GMK70_RADIO)
  zephyr_library()
  zephyr_library_include_directories(. ../wired ../update
    ../../qmk-common/keyboards/zuoya/gmk70/update ${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(core.c selection.c radio.c endpoint.c behavior.c control.c)
  zephyr_ld_options(-Wl,--wrap=zmk_endpoint_send_report)
  zephyr_ld_options(-Wl,--wrap=zmk_hid_keyboard_clear)
  zephyr_ld_options(-Wl,--wrap=zmk_hid_consumer_clear)
  zephyr_ld_options(-Wl,--wrap=zmk_keymap_set_layer_binding_at_idx)
endif()

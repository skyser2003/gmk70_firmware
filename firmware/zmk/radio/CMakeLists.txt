# SPDX-License-Identifier: GPL-2.0-or-later
if(CONFIG_GMK70_RADIO)
  zephyr_library()
  zephyr_library_include_directories(. ../wired ../update ../nkro
    ../../qmk-common/drivers/wireless
    ../../qmk-common/keyboards/zuoya/gmk70/update ${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(core.c selection.c radio.c endpoint.c behavior.c control.c)
  zephyr_ld_options(-Wl,--wrap=zmk_endpoint_send_report)
  zephyr_ld_options(-Wl,--wrap=zmk_hid_keyboard_clear)
  zephyr_ld_options(-Wl,--wrap=zmk_hid_consumer_clear)
  zephyr_ld_options(-Wl,--wrap=zmk_keymap_set_layer_binding_at_idx)
  zephyr_ld_options(-Wl,--wrap=zmk_keymap_discard_changes)
  zephyr_ld_options(-Wl,--wrap=zmk_keymap_reset_settings)
  zephyr_ld_options(-Wl,--wrap=zmk_keymap_move_layer)
  zephyr_ld_options(-Wl,--wrap=zmk_keymap_add_layer)
  zephyr_ld_options(-Wl,--wrap=zmk_keymap_remove_layer)
  zephyr_ld_options(-Wl,--wrap=zmk_keymap_restore_layer)
endif()

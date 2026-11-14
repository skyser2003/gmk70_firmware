# SPDX-License-Identifier: Apache-2.0
add_subdirectory(drivers/gpio)
add_subdirectory(drivers/usb/device)
if(CONFIG_BOARD_GMK70_WIRED)
  zephyr_include_directories(include)
  set(gmk_generated ${CMAKE_BINARY_DIR}/gmk70-generated)
  set(gmk_keyboard ${CMAKE_CURRENT_LIST_DIR}/../qmk-common/keyboards/zuoya/gmk70/keyboard.json)
  set(gmk_pin_generator ${CMAKE_CURRENT_LIST_DIR}/tools/wired_pins.py)
  add_custom_command(OUTPUT ${gmk_generated}/gmk70_wired_pins.h
    COMMAND ${PYTHON_EXECUTABLE} ${gmk_pin_generator}
      --keyboard ${gmk_keyboard} --output ${gmk_generated}/gmk70_wired_pins.h
    DEPENDS ${gmk_keyboard} ${gmk_pin_generator}
    VERBATIM)
  add_custom_target(gmk70_wired_pins DEPENDS ${gmk_generated}/gmk70_wired_pins.h)
  zephyr_include_directories(${gmk_generated})
  add_subdirectory(drivers/kscan)
  zephyr_library()
  zephyr_library_include_directories(wired)
  zephyr_library_include_directories(${ZEPHYR_BASE}/kernel/include ${ZEPHYR_BASE}/arch/arm/include)
  zephyr_library_sources(wired/wired_protocol.c wired/wired_wb32.c wired/encoder.c wired/encoder_native.c)
  if(CONFIG_GMK70_ZMK_STUDIO_SETTINGS)
    zephyr_library_sources(runtime/crc.c)
    zephyr_ld_options(-Wl,--wrap=crc32_ieee)
  endif()
  zephyr_library_sources_ifdef(CONFIG_GMK70_EVENT_IDLE wired/event_link.c runtime/idle.c runtime/main.c)
  if(CONFIG_GMK70_EVENT_IDLE)
    zephyr_ld_options(-Wl,--wrap=main)
  endif()
  if(CONFIG_LTO)
    # Defer board inlining until the full program is visible, and prefer
    # callee-save registers over repeated call-site spills in this small image.
    zephyr_library_compile_options(-fno-early-inlining)
    zephyr_compile_options(-fipa-pta -fno-caller-saves -fno-early-inlining)
    # Retain native/public --wrap boundaries outside LTO below. Within the
    # remaining closed units, GCC may specialize internal parameters; this
    # keeps bounded radio reconnect inside the unchanged updater slot.
    zephyr_ld_options(-flto-partition=one -fipa-sra)
    if(CONFIG_GMK70_RADIO)
      zephyr_library_sources(runtime/native_endpoints.c)
    endif()
    if(CONFIG_GMK70_RADIO OR CONFIG_GMK70_MERGED_CONTROLS)
      zephyr_library_sources(runtime/native_keymap.c)
    endif()
    if(CONFIG_GMK70_MERGED_CONTROLS)
      zephyr_library_sources(runtime/native_behavior.c)
    endif()
    if(CONFIG_GMK70_WB32_RGB)
      zephyr_library_sources(runtime/native_rgb.c)
    endif()
    # The updater's fixed application slot must also fit event-driven idle.
    # Wrapped native definitions MUST stay outside LTO: even noipa cannot
    # preserve GNU --wrap when LTO binds a call to its original definition.
    # Maintained board owners share optimization; linked image checks retain
    # explicit native initialization, clock, storage and routing boundaries.
    function(gmk_preserve_wrap_boundaries)
      get_property(gmk_libraries GLOBAL PROPERTY ZEPHYR_LIBS)
      list(APPEND gmk_libraries app subsys__usb__device__class__hid)
      list(REMOVE_DUPLICATES gmk_libraries)
      foreach(gmk_library IN LISTS gmk_libraries)
        if(TARGET ${gmk_library} AND (gmk_library MATCHES "firmware__zmk" OR gmk_library STREQUAL "app" OR gmk_library STREQUAL "subsys__usb__device__class__hid"))
          get_target_property(gmk_sources ${gmk_library} SOURCES)
          get_target_property(gmk_directory ${gmk_library} SOURCE_DIR)
          foreach(gmk_source IN LISTS gmk_sources)
            if(gmk_source MATCHES "\\.c$")
              if(NOT IS_ABSOLUTE "${gmk_source}")
                set(gmk_source "${gmk_directory}/${gmk_source}")
              endif()
              set(gmk_boundary OFF)
              if(gmk_library STREQUAL "app")
                if(CONFIG_GMK70_RADIO AND gmk_source MATCHES "/endpoints\\.c$")
                  # Public wrapped exports live in the non-LTO shim above.
                  # Macro renaming preserves native same-TU call semantics.
                  set_property(SOURCE "${gmk_source}" TARGET_DIRECTORY app APPEND PROPERTY COMPILE_DEFINITIONS
                    zmk_endpoint_send_report=gmk_native_endpoint_send_report
                    zmk_endpoint_send_mouse_report=gmk_native_endpoint_send_mouse_report)
                  set_property(SOURCE "${gmk_source}" TARGET_DIRECTORY app APPEND PROPERTY COMPILE_OPTIONS
                    -include "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/runtime/native_endpoints_boundary.h")
                elseif((CONFIG_GMK70_RADIO OR CONFIG_GMK70_MERGED_CONTROLS) AND gmk_source MATCHES "/keymap\\.c$")
                  set_property(SOURCE "${gmk_source}" TARGET_DIRECTORY app APPEND PROPERTY COMPILE_DEFINITIONS
                    zmk_keymap_set_layer_binding_at_idx=gmk_native_keymap_set_layer_binding_at_idx
                    zmk_keymap_discard_changes=gmk_native_keymap_discard_changes
                    zmk_keymap_reset_settings=gmk_native_keymap_reset_settings
                    zmk_keymap_add_layer=gmk_native_keymap_add_layer
                    zmk_keymap_remove_layer=gmk_native_keymap_remove_layer
                    zmk_keymap_move_layer=gmk_native_keymap_move_layer
                    zmk_keymap_restore_layer=gmk_native_keymap_restore_layer)
                  set_property(SOURCE "${gmk_source}" TARGET_DIRECTORY app APPEND PROPERTY COMPILE_OPTIONS
                    -include "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/runtime/native_keymap_boundary.h")
                elseif(CONFIG_GMK70_MERGED_CONTROLS AND gmk_source MATCHES "/behavior\\.c$")
                  set_property(SOURCE "${gmk_source}" TARGET_DIRECTORY app APPEND PROPERTY COMPILE_DEFINITIONS
                    zmk_behavior_find_behavior_name_from_local_id=gmk_native_behavior_find_behavior_name_from_local_id)
                elseif(CONFIG_GMK70_WB32_RGB AND gmk_source MATCHES "/rgb_underglow\\.c$")
                  set_property(SOURCE "${gmk_source}" TARGET_DIRECTORY app APPEND PROPERTY COMPILE_DEFINITIONS
                    zmk_rgb_underglow_set_hsb=gmk_native_rgb_underglow_set_hsb
                    zmk_rgb_underglow_on=gmk_native_rgb_underglow_on
                    zmk_rgb_underglow_off=gmk_native_rgb_underglow_off
                    zmk_rgb_underglow_toggle=gmk_native_rgb_underglow_toggle)
                elseif(gmk_source MATCHES "/(hid|keymap|endpoints|rgb_underglow|pm|usb|settings|main)\\.c$"
                    OR gmk_source MATCHES "/events/(position_state_changed|keycode_state_changed|activity_state_changed)\\.c$"
                    OR gmk_source MATCHES "/studio/core\\.c$")
                  set(gmk_boundary ON)
                endif()
              elseif(gmk_library STREQUAL "subsys__usb__device__class__hid")
                set(gmk_boundary ON)
              elseif(NOT gmk_source MATCHES "/(event_link|wired_protocol|wired_wb32|encoder|encoder_native|split|raw_hid|studio_feature|nkro|flip|lighting|lighting_frame|wb32_pixels|image|radio|battery|battery_sensor|battery_view|bool_setting|settings|settings_store|settings_backend|receiver|service|storage|idle|retained|retained_clock|rgb_settings|endpoint|radio_keys|battery_holds|stock_power|battery_status|kscan_gmk70_wired|power|platform|wb32_flash|native_update_wire|usb_dc_wb32|gpio_wb32|behavior|bindings|metadata|control|controls|selection|protocol|coordinator|core)\\.c$"
                  OR gmk_source MATCHES "/recovery/startup\\.c$")
                set(gmk_boundary ON)
              endif()
              if(gmk_boundary)
                set_property(SOURCE "${gmk_source}" TARGET_DIRECTORY ${gmk_library} APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
              endif()
            endif()
          endforeach()
        endif()
      endforeach()
      set_property(SOURCE "${ZEPHYR_BASE}/lib/os/reboot.c" "${ZEPHYR_BASE}/subsys/settings/src/settings_store.c"
        TARGET_DIRECTORY zephyr APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
      if(CONFIG_GMK70_ZMK_STUDIO_SETTINGS)
        set_property(SOURCE "${ZEPHYR_BASE}/lib/crc/crc32_sw.c"
          TARGET_DIRECTORY zephyr APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
      endif()
      set_property(SOURCE "${ZEPHYR_BASE}/drivers/timer/cortex_m_systick.c" "${ZEPHYR_BASE}/drivers/timer/sys_clock_init.c"
        TARGET_DIRECTORY drivers__timer APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
      set_property(SOURCE "${ZEPHYR_BASE}/kernel/main_weak.c"
        TARGET_DIRECTORY kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endfunction()
    cmake_language(DEFER DIRECTORY ${APPLICATION_SOURCE_DIR} CALL gmk_preserve_wrap_boundaries)
  endif()
  if(CONFIG_GMK70_NATIVE_UPDATE_WIRE)
    zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include ${APPLICATION_SOURCE_DIR}/src/split
      ../qmk-common/keyboards/zuoya/gmk70/update)
    zephyr_library_sources(wired/native_update_wire.c ${APPLICATION_SOURCE_DIR}/src/split/wired/wired.c)
    zephyr_library_sources_ifdef(CONFIG_GMK70_NATIVE_UPDATE_LEGACY_BRIDGE wired/native_update_bridge.c)
  endif()
  if(CONFIG_GMK70_NATIVE_SPLIT)
    zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
    zephyr_library_sources(wired/split.c)
    # Upstream selects one generic role at build time. Both remain unmodified;
    # only the peripheral TU's private-in-practice global receives a unique name.
    set(gmk_peripheral_core ${APPLICATION_SOURCE_DIR}/src/split/peripheral.c)
    target_sources(app PRIVATE ${gmk_peripheral_core})
    set_source_files_properties(${gmk_peripheral_core} TARGET_DIRECTORY app PROPERTIES
      COMPILE_DEFINITIONS active_transport=gmk_native_peripheral_transport)
    zephyr_linker_sources(SECTIONS ${APPLICATION_SOURCE_DIR}/include/linker/zmk-split-transport-peripheral.ld)
    zephyr_ld_options(-Wl,--wrap=raise_zmk_position_state_changed)
  endif()
  if(CONFIG_GMK70_NKRO OR CONFIG_GMK70_RADIO)
    zephyr_library_sources(../qmk-common/drivers/wireless/radio_keys.c)
  endif()
  if(CONFIG_GMK70_NKRO OR CONFIG_GMK70_FUNCTION_FLIP)
    zephyr_library_include_directories(update ../qmk-common/keyboards/zuoya/gmk70/update)
    zephyr_library_sources(studio/bool_setting.c)
  endif()
  zephyr_library_include_directories(lighting)
  zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/radio)
  add_dependencies(${ZEPHYR_CURRENT_LIBRARY} gmk70_wired_pins)
  add_subdirectory(update)
  add_subdirectory(runtime)
  add_subdirectory(studio)
  add_subdirectory(lighting)
  add_subdirectory(battery)
  add_subdirectory(radio)
  add_subdirectory(function_flip)
  add_subdirectory(nkro)
  if(CONFIG_GMK70_MERGED_CONTROLS)
    zephyr_library_sources(controls/bindings.c)
    zephyr_library_sources_ifdef(CONFIG_ZMK_BEHAVIOR_METADATA controls/metadata.c)
    zephyr_ld_options(-Wl,--wrap=zmk_behavior_find_behavior_name_from_local_id)
  endif()
  if(CONFIG_GMK70_CONTROLS)
    zephyr_library_sources(controls/controls.c controls/behavior.c ../qmk-common/keyboards/zuoya/gmk70/stock_power.c)
  endif()
  if(CONFIG_GMK70_RECOVERY)
    add_subdirectory(recovery)
  endif()
  if(CONFIG_GMK70_POWER)
    add_subdirectory(power)
  endif()
  zephyr_linker_sources(SECTIONS SORT_KEY zzzz_gmk_image_end update/image_end.ld)
endif()
if(CONFIG_GMK70_TRIAL_MAIN_RETURN)
  zephyr_library()
  zephyr_library_sources(trial/main_return.c)
  zephyr_ld_options(-Wl,--wrap=main)
endif()

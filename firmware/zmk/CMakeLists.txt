# SPDX-License-Identifier: Apache-2.0
add_subdirectory(drivers/gpio)
add_subdirectory(drivers/usb/device)
if(CONFIG_BOARD_GMK70_WIRED)
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
  zephyr_library_sources(wired/wired_protocol.c wired/wired_wb32.c)
  add_dependencies(${ZEPHYR_CURRENT_LIBRARY} gmk70_wired_pins)
  add_subdirectory(update)
  add_subdirectory(studio)
  zephyr_linker_sources(SECTIONS update/image_end.ld)
endif()
if(CONFIG_GMK70_TRIAL_MAIN_RETURN)
  zephyr_library()
  zephyr_library_sources(trial/main_return.c)
  zephyr_ld_options(-Wl,--wrap=main)
endif()

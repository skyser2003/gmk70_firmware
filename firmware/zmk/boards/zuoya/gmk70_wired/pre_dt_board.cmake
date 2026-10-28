# SPDX-License-Identifier: GPL-2.0-or-later
# The hook runs before DTS preprocessing, including on Ninja reconfiguration.
if(GMK70_STUDIO)
  get_filename_component(gmk_root "${BOARD_DIR}/../../../../.." ABSOLUTE)
  set(gmk_layout_generator "${gmk_root}/firmware/zmk/tools/studio_layout.py")
  set(gmk_keyboard "${gmk_root}/firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json")
  set(gmk_via "${gmk_root}/definitions/via/ZUOYA_GMK70.json")
  set(gmk_layout "${CMAKE_BINARY_DIR}/gmk70-generated/studio-layout.overlay")
  execute_process(COMMAND "${PYTHON_EXECUTABLE}" "${gmk_layout_generator}"
    --keyboard "${gmk_keyboard}" --via "${gmk_via}" --output "${gmk_layout}"
    COMMAND_ERROR_IS_FATAL ANY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${gmk_layout_generator}" "${gmk_keyboard}" "${gmk_via}")
  list(APPEND EXTRA_DTC_OVERLAY_FILE "${gmk_layout}")
endif()

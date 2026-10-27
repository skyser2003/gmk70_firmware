# SPDX-License-Identifier: Apache-2.0
add_subdirectory(drivers/gpio)
add_subdirectory(drivers/usb/device)
if(CONFIG_GMK70_TRIAL_MAIN_RETURN)
  zephyr_library()
  zephyr_library_sources(trial/main_return.c)
  zephyr_ld_options(-Wl,--wrap=main)
endif()

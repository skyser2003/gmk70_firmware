# SPDX-License-Identifier: Apache-2.0
if(CONFIG_GMK70_WB32_RGB)
  zephyr_library()
  zephyr_library_include_directories(. ../wired ${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(lighting_frame.c lighting.c wb32_pixels.c rgb_settings.c)
  zephyr_library_sources_ifdef(CONFIG_GMK70_BATTERY battery_view.c behavior_battery.c)
  zephyr_ld_options(-Wl,--wrap=zmk_rgb_underglow_set_hsb)
endif()

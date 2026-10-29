# SPDX-License-Identifier: GPL-2.0-or-later
if(CONFIG_GMK70_BATTERY)
  zephyr_library()
  zephyr_library_include_directories(. ../wired)
  zephyr_library_sources(battery.c battery_sensor.c)
endif()

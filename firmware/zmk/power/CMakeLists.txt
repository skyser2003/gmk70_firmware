# SPDX-License-Identifier: GPL-2.0-or-later
zephyr_library_sources(core.c protocol.c coordinator.c power.c
  ../../qmk-common/drivers/wireless/wb32_stop.c
  ../../qmk-common/drivers/wireless/wb32_stop_hw.c)
zephyr_library_include_directories(../../qmk-common/drivers/wireless)
zephyr_ld_options(-Wl,--wrap=zmk_pm_suspend_devices
  -Wl,--wrap=zmk_pm_resume_devices -Wl,--wrap=zmk_pm_soft_off
  -Wl,--wrap=zmk_usb_get_conn_state)

zephyr_library_sources_ifdef(CONFIG_GMK70_RETAINED_SLEEP retained.c retained_clock.c)
if(CONFIG_GMK70_RETAINED_SLEEP)
  zephyr_ld_options(-Wl,--wrap=sys_clock_cycle_get_32 -Wl,--wrap=sys_clock_cycle_get_64)
endif()

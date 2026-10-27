# SPDX-License-Identifier: GPL-2.0-or-later
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_KSCAN_GMK70_WIRED kscan_gmk70_wired.c)
zephyr_library_include_directories(../../wired)

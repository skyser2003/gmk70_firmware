# SPDX-License-Identifier: GPL-2.0-or-later
zephyr_library_sources(core.c behavior.c startup.c)
zephyr_ld_options(-Wl,--wrap=sys_reboot -Wl,--wrap=zmk_settings_erase)

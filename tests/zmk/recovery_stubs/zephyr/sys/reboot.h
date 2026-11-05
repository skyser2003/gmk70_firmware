/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#define SYS_REBOOT_WARM 0
#define SYS_REBOOT_COLD 1
_Noreturn void sys_reboot(int type);

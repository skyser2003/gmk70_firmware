// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
typedef uint32_t syssts_t;
syssts_t         chSysGetStatusAndLockX(void);
void             chSysRestoreStatusX(syssts_t status);

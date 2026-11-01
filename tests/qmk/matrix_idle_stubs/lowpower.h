// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
typedef enum { LPWR_NORMAL, LPWR_PRESLEEP, LPWR_STOP, LPWR_WAKEUP } lpwr_state_t;
lpwr_state_t lpwr_get_state(void);

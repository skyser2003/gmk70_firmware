/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
bool usb_get_remote_wakeup_status(void);
int  usb_wakeup_request(void);

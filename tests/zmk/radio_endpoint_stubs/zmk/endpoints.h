/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>

int  zmk_endpoint_send_report(uint16_t usage_page);
void zmk_endpoint_clear_reports(void);

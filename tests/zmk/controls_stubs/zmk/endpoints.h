/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zmk/endpoints_types.h>
#define ZMK_ENDPOINT_COUNT 1
struct zmk_endpoint_instance zmk_endpoint_get_selected(void);
int                          zmk_endpoint_instance_to_index(struct zmk_endpoint_instance endpoint);
int                          zmk_endpoint_send_report(uint16_t page);

/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zmk/endpoints.h>
/* This function is not wrapped. Keep its native HID-clear call sites visible
 * for the image check while allowing other endpoint helpers to share LTO. */
extern __typeof__(zmk_endpoint_clear_reports) zmk_endpoint_clear_reports __attribute__((noipa));

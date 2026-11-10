/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <zmk/endpoints.h>

/* Non-LTO public definitions preserve GNU --wrap resolution. The untouched
 * native implementation is compiled under unique names; no endpoint logic is
 * duplicated here. See native_endpoints_boundary.h and the image checks. */
extern __typeof__(zmk_endpoint_send_report) gmk_native_endpoint_send_report;
int                                         zmk_endpoint_send_report(uint16_t usage_page) {
    return gmk_native_endpoint_send_report(usage_page);
}
#if IS_ENABLED(CONFIG_ZMK_POINTING)
extern __typeof__(zmk_endpoint_send_mouse_report) gmk_native_endpoint_send_mouse_report;
int                                               zmk_endpoint_send_mouse_report(void) {
    return gmk_native_endpoint_send_mouse_report();
}
#endif

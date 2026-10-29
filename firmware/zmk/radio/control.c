/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "control.h"
#include "radio.h"
#include <errno.h>
#include <string.h>

#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
extern int gmk_radio_test_marker(void);
#endif

bool gmk_radio_control_packet(const uint8_t request[32]) {
    if (request[0] != 0x7f || request[1] != 0x72 || request[2] != 1 || request[3] != 0 || request[4] != 0x22 || request[5] > 3 || request[7] || !gmk_read_u32(request + 8)) return false;
    const uint8_t action = request[5], channel = request[6];
    if ((action == 1 && channel > 6) || (action == 2 && (channel < 1 || channel > 6)) || ((action == 0 || action == 3) && channel != 0)) return false;
    for (unsigned i = 12; i < 32; ++i)
        if (request[i]) return false;
    return true;
}
void gmk_radio_control_receive(const uint8_t request[32], uint8_t response[32], bool usb, const gmk_zmk_update_env_t *env) {
    memset(response, 0, 32);
    if (!gmk_radio_control_packet(request)) return;
    memcpy(response, request, 5);
    memcpy(response + 8, request + 8, 4);
    memcpy(response + 12, "RADI", 4);
    response[5] = 2;
    response[7] = env->left ? 3 : 0;
    if (!usb || !env->left || !env->usb_configured) return;
    if (request[5] && (!env->wired || !env->writes_ready)) return;
    int result = 0;
    if (request[5]) {
        if (request[5] == 1 || request[5] == 2)
            result = gmk_radio_select(request[6], request[5] == 2);
        else {
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
            result = gmk_radio_test_marker();
#else
            result = -ENOTSUP;
#endif
        }
    }
    response[5] = result == 0 ? 0 : result == -EBUSY ? 1 : 2;
    /* Selection/marker callbacks only queue work. Report the current cache,
     * without implying that an accepted asynchronous request has completed. */
    struct gmk_radio_status status;
    gmk_radio_get(&status);
    response[6] = (status.waiting ? 1U : 0U) | (status.available ? 2U : 0U);
#ifdef CONFIG_GMK70_RADIO_QUALIFICATION
    response[6] |= 4U;
#endif
    response[16] = status.selected;
    response[17] = status.active;
    response[18] = status.connection;
    response[19] = status.queued;
    gmk_write_u32(response + 20, status.errors);
    gmk_write_u32(response + 24, status.selections);
    gmk_write_u32(response + 28, status.reports);
}

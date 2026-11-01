/* SPDX-License-Identifier: GPL-2.0-or-later */
#define DT_DRV_COMPAT zuoya_gmk70_wired
#include <zephyr/device.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include "wired.h"
#include "virtual_keys.h"
#include "../../lighting/lighting.h"

struct gmk_kscan_data {
    kscan_callback_t callback;
    bool             enabled;
    uint16_t         previous[10];
    kscan_callback_t encoder_release;
    uint32_t         encoder_at;
    uint8_t          encoder_key;
    bool             encoder_gap;
};
static const struct device *scanner;
static int                  configure(const struct device *dev, kscan_callback_t callback) {
    struct gmk_kscan_data *data = dev->data;
    if (!callback) return -EINVAL;
    unsigned key   = irq_lock();
    data->callback = callback;
    irq_unlock(key);
    return 0;
}
static int enable(const struct device *dev) {
    struct gmk_kscan_data *data = dev->data;
    unsigned               key  = irq_lock();
    data->enabled               = true;
    irq_unlock(key);
    return 0;
}
static int disable(const struct device *dev) {
    struct gmk_kscan_data *data = dev->data;
    unsigned               key  = irq_lock();
    data->enabled               = false;
    irq_unlock(key);
    return 0;
}
static void encoder_step(struct gmk_kscan_data *data, kscan_callback_t callback, bool enabled) {
    uint32_t now     = k_uptime_get_32();
    bool     allowed = enabled && callback && gmk_wired_is_left() && gmk_split_available();
    if (!allowed) gmk_wired_encoder_clear();
    if (data->encoder_key) {
        if (!allowed || (uint32_t)(now - data->encoder_at) >= 5) {
            /* Pair with the original callback even across disable/remapping. */
            data->encoder_release(scanner, GMK_ENCODER_ROW, data->encoder_key - 1, false);
            data->encoder_key = 0;
            data->encoder_at  = now;
            data->encoder_gap = true;
        }
        return;
    }
    if (!allowed || (data->encoder_gap && (uint32_t)(now - data->encoder_at) < 5)) return;
    int direction = gmk_wired_encoder_next();
    if (!direction) return;
    data->encoder_key     = (direction < 0 ? GMK_ENCODER_CCW_COL : GMK_ENCODER_CW_COL) + 1;
    data->encoder_release = callback;
    data->encoder_at      = now;
    data->encoder_gap     = false;
    callback(scanner, GMK_ENCODER_ROW, data->encoder_key - 1, true);
}
static void scan_once(void) {
    uint16_t rows[10] = {0};
    unsigned offset   = gmk_wired_is_left() ? 0 : 5;
    gmk_wired_scan(rows + offset);
    if (gmk_wired_is_left()) {
        gmk_wired_poll();
        gmk_wired_remote_matrix(rows + 5);
    }
    gmk_lighting_poll();
    if (scanner) {
        struct gmk_kscan_data *data     = scanner->data;
        unsigned               key      = irq_lock();
        kscan_callback_t       callback = data->callback;
        bool                   enabled  = data->enabled;
        irq_unlock(key);
        if (enabled && callback)
            for (unsigned r = 0; r < 10; ++r)
                for (unsigned c = 0; c < 9; ++c) {
                    uint16_t mask = (uint16_t)(1U << c);
                    if ((rows[r] ^ data->previous[r]) & mask) callback(scanner, r, c, (rows[r] & mask) != 0);
                }
        if (enabled && callback)
            for (unsigned r = 0; r < 10; ++r)
                data->previous[r] = rows[r];
        encoder_step(data, callback, enabled);
    }
}
static void scan_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    while (true) {
        scan_once();
        k_sleep(K_MSEC(1));
    }
}
K_THREAD_DEFINE(gmk_scan_thread, 1536, scan_thread, NULL, NULL, NULL, 5, 0, 0);
static int init(const struct device *dev) {
    scanner = dev;
    return 0;
}
static const struct kscan_driver_api api = {.config = configure, .enable_callback = enable, .disable_callback = disable};
static struct gmk_kscan_data         data;
DEVICE_DT_INST_DEFINE(0, init, NULL, &data, NULL, POST_KERNEL, 90, &api);

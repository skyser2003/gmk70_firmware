/* SPDX-License-Identifier: GPL-2.0-or-later */
#define DT_DRV_COMPAT zuoya_gmk70_battery
#include "battery.h"
#include "wired.h"
#include <errno.h>
#include <zephyr/drivers/sensor.h>
static int battery_sensor_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return gmk_wired_is_left() ? 0 : -ENODEV;
}
static int battery_sample_fetch(const struct device *dev, enum sensor_channel channel) {
    ARG_UNUSED(dev);
    if (channel != SENSOR_CHAN_ALL && channel != SENSOR_CHAN_GAUGE_STATE_OF_CHARGE) return -ENOTSUP;
    struct gmk_battery_status status;
    gmk_battery_get(&status);
    return status.valid ? 0 : -ENODATA;
}
static int battery_channel_get(const struct device *dev, enum sensor_channel channel, struct sensor_value *value) {
    ARG_UNUSED(dev);
    if (channel != SENSOR_CHAN_GAUGE_STATE_OF_CHARGE) return -ENOTSUP;
    struct gmk_battery_status status;
    gmk_battery_get(&status);
    if (!status.valid) return -ENODATA;
    *value = (struct sensor_value){.val1 = status.percentage, .val2 = 0};
    return 0;
}
static const struct sensor_driver_api battery_sensor_api = {
    .sample_fetch = battery_sample_fetch,
    .channel_get  = battery_channel_get,
};
DEVICE_DT_INST_DEFINE(0, battery_sensor_init, NULL, NULL, NULL, POST_KERNEL, 90, &battery_sensor_api);

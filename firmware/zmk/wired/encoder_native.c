/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "encoder.h"
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

static const struct device *const knob = DEVICE_DT_GET(DT_NODELABEL(gmk_knob));

bool gmk_native_encoder_init(void) {
    return device_init(knob) == 0 && device_is_ready(knob);
}

int32_t gmk_native_encoder_sample(void) {
    struct sensor_value value;
    if (sensor_sample_fetch(knob) || sensor_channel_get(knob, SENSOR_CHAN_ROTATION, &value)) return 0;
    return value.val1;
}

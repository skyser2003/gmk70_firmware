/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <zephyr/device.h>
#include <zmk/behavior.h>
struct behavior_parameter_value_metadata {
    char *display_name;
    union {
        uint32_t value;
        struct {
            int32_t min, max;
        } range;
    };
    enum {
        BEHAVIOR_PARAMETER_VALUE_TYPE_NIL,
        BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
        BEHAVIOR_PARAMETER_VALUE_TYPE_HID_USAGE,
        BEHAVIOR_PARAMETER_VALUE_TYPE_LAYER_ID,
    } type;
};
struct behavior_parameter_metadata_set {
    size_t                                          param1_values_len;
    const struct behavior_parameter_value_metadata *param1_values;
    size_t                                          param2_values_len;
    const struct behavior_parameter_value_metadata *param2_values;
};
struct behavior_parameter_metadata {
    size_t                                        sets_len;
    const struct behavior_parameter_metadata_set *sets;
};
enum behavior_locality { BEHAVIOR_LOCALITY_CENTRAL, BEHAVIOR_LOCALITY_EVENT_SOURCE, BEHAVIOR_LOCALITY_GLOBAL };
typedef int (*behavior_keymap_binding_callback_t)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
enum behavior_sensor_binding_process_mode { BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_TRIGGER, BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_DISCARD };
struct zmk_sensor_config;
struct zmk_sensor_channel_data;
typedef int (*behavior_sensor_keymap_binding_process_callback_t)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event, enum behavior_sensor_binding_process_mode);
typedef int (*behavior_sensor_keymap_binding_accept_data_callback_t)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event, const struct zmk_sensor_config *, size_t, const struct zmk_sensor_channel_data *);
struct behavior_driver_api {
    enum behavior_locality                                locality;
    behavior_keymap_binding_callback_t                    binding_convert_central_state_dependent_params;
    behavior_keymap_binding_callback_t                    binding_pressed;
    behavior_keymap_binding_callback_t                    binding_released;
    behavior_sensor_keymap_binding_accept_data_callback_t sensor_binding_accept_data;
    behavior_sensor_keymap_binding_process_callback_t     sensor_binding_process;
#if CONFIG_ZMK_BEHAVIOR_METADATA
    int (*get_parameter_metadata)(const struct device *, struct behavior_parameter_metadata *);
    const struct behavior_parameter_metadata *parameter_metadata;
#endif
};
#define BEHAVIOR_DT_INST_DEFINE(inst, init, pm, data, config, level, priority, api) static const struct behavior_driver_api *const registered_api = (api)

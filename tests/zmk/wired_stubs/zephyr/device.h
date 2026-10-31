#pragma once
struct device {
    void       *data;
    const void *api;
    const void *config;
};
extern const struct device fake_gpio_ports[3];
#define DT_NODELABEL(name) DT_LABEL_##name
#define DT_LABEL_gpioa 0
#define DT_LABEL_gpiob 1
#define DT_LABEL_gpioc 2
#define DEVICE_DT_GET(node) (&fake_gpio_ports[node])
#define DT_INST_FOREACH_STATUS_OKAY(fn)
static inline int device_is_ready(const struct device *dev) {
    return dev != 0;
}
#define DEVICE_API(type, name) const struct type##_driver_api name
#define DEVICE_DT_INST_DEFINE(n, init, pm, data, config, level, priority, api) static void *const device_refs[] __attribute__((used)) = {(void *)&init, (void *)data, (void *)api}

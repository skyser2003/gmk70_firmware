#pragma once
struct device {
    void       *data;
    const void *api;
};
#define DEVICE_DT_INST_DEFINE(n, init, pm, data, config, level, priority, api) static void *const device_refs[] __attribute__((used)) = {(void *)&init, (void *)data, (void *)api}

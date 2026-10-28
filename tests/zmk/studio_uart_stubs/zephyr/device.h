#pragma once
struct device {
    int unused;
};
extern struct device test_uart;
#define DT_CHOSEN(x) 0
#define DEVICE_DT_GET(x) (&test_uart)
static inline int device_is_ready(const struct device *dev) {
    (void)dev;
    return 1;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct gmk_battery_status {
    uint32_t age_ms, queries, replies, errors;
    uint8_t  percentage;
    bool     valid, cable, full;
};

#ifdef CONFIG_GMK70_BATTERY
void gmk_battery_poll(void);
void gmk_battery_get(struct gmk_battery_status *out);
/* Call from the existing service/UART owner, never from an IRQ or input worker. */
bool gmk_battery_idle(void);
bool gmk_battery_power_quiesce(bool enabled);
#else
static inline void gmk_battery_poll(void) {}
static inline void gmk_battery_get(struct gmk_battery_status *out) {
    *out = (struct gmk_battery_status){0};
}
static inline bool gmk_battery_idle(void) {
    return true;
}
static inline bool gmk_battery_power_quiesce(bool enabled) {
    (void)enabled;
    return true;
}
#endif

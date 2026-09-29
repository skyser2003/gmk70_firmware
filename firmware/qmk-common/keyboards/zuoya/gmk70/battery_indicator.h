// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    BATTERY_INDICATOR_COMMAND = 0x56,
    BATTERY_INDICATOR_LEVEL   = 1 << 0,
    BATTERY_INDICATOR_CHARGE  = 1 << 1,
    BATTERY_INDICATOR_FULL    = 1 << 2,
};

// Only effective display requests cross the split: USB suppresses the level
// request, and an unplugged charging cable suppresses the charging request.
typedef struct {
    uint8_t flags;
    uint8_t percent;
} battery_indicator_state_t;

typedef struct {
    battery_indicator_state_t peer;
    battery_indicator_state_t sent;
    uint32_t                  received_at;
    uint32_t                  attempted_at;
    uint32_t                  sent_at;
    bool                      received;
    bool                      attempted;
    bool                      acknowledged;
} battery_indicator_sync_t;

typedef struct {
    void (*set_color)(int index, uint8_t red, uint8_t green, uint8_t blue);
    void (*set_all)(uint8_t red, uint8_t green, uint8_t blue);
} battery_indicator_renderer_t;

battery_indicator_state_t battery_indicator_state(bool level, bool charge, bool wireless, bool charging, bool full, uint8_t percent);
bool                      battery_indicator_prepare(battery_indicator_sync_t *sync, battery_indicator_state_t state, uint32_t now, uint8_t body[4]);
void                      battery_indicator_acknowledge(battery_indicator_sync_t *sync, battery_indicator_state_t state, uint32_t now);
bool                      battery_indicator_receive(battery_indicator_sync_t *sync, const uint8_t body[4], uint32_t now);
battery_indicator_state_t battery_indicator_peer(const battery_indicator_sync_t *sync, uint32_t now);
uint8_t                   battery_indicator_render(battery_indicator_state_t state, const battery_indicator_renderer_t *renderer);

// Board integration, also used by the diagnostic getter.
void                      gmk70_battery_request(bool level, bool pressed);
bool                      gmk70_battery_requested(bool level);
battery_indicator_state_t gmk70_battery_local_state(void);
bool                      gmk70_battery_overlay_allowed(void);
void                      gmk70_battery_task(void);
bool                      gmk70_battery_receive(const uint8_t body[4]);

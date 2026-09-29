// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BATTERY_DIAG_ENTERED            = 0,
    BATTERY_DIAG_RECORDING_CONSUMED = 1,
    BATTERY_DIAG_DEVICE_CONSUMED    = 2,
    BATTERY_DIAG_LEVEL_HANDLED      = 3,
    BATTERY_DIAG_CHARGING_HANDLED   = 4,
} battery_diag_stage_t;

typedef enum {
    BATTERY_DIAG_INDICATOR     = 0,
    BATTERY_DIAG_LEVEL_DRAW    = 1,
    BATTERY_DIAG_CHARGING_DRAW = 2,
} battery_diag_render_t;

#ifdef GMK70_BATTERY_DIAGNOSTICS
typedef enum {
    BATTERY_DIAG_MASTER             = 1 << 0,
    BATTERY_DIAG_LEFT               = 1 << 1,
    BATTERY_DIAG_CHARGING           = 1 << 2,
    BATTERY_DIAG_FULL               = 1 << 3,
    BATTERY_DIAG_RECORDING          = 1 << 4,
    BATTERY_DIAG_LEVEL_REQUEST      = 1 << 5,
    BATTERY_DIAG_CHARGING_REQUEST   = 1 << 6,
    BATTERY_DIAG_INDICATORS_ENABLED = 1 << 7,
    BATTERY_DIAG_INQUIRY_BLOCK      = 1 << 8,
    BATTERY_DIAG_WHITE_TEST         = 1 << 9,
    BATTERY_DIAG_RGB_ENABLED        = 1 << 10,
    BATTERY_DIAG_RGB_SUSPENDED      = 1 << 11,
} battery_diag_flag_t;

typedef struct {
    uint32_t layers;
    uint32_t default_layers;
    uint16_t flags;
    uint8_t  board_device;
    uint8_t  radio_device;
    uint8_t  radio_state;
    uint8_t  battery;
    uint8_t  peer_battery;
    uint8_t  rgb_mode;
    uint8_t  brightness;
    uint8_t  led_flags;
    uint8_t  pins;
} battery_diag_snapshot_t;

battery_diag_snapshot_t battery_diagnostics_snapshot(void);
void                    battery_diagnostics_key(uint16_t keycode, bool pressed, battery_diag_stage_t stage);
void                    battery_diagnostics_render(battery_diag_render_t render);
void                    battery_diagnostics_frame(uint8_t rendered, uint8_t before_r, uint8_t before_g, uint8_t before_b, uint8_t after_r, uint8_t after_g, uint8_t after_b);
bool                    battery_diagnostics_command(uint8_t *data, uint8_t length);
#else
static inline void battery_diagnostics_key(uint16_t keycode, bool pressed, battery_diag_stage_t stage) {
    (void)keycode;
    (void)pressed;
    (void)stage;
}
static inline void battery_diagnostics_render(battery_diag_render_t render) {
    (void)render;
}
#endif

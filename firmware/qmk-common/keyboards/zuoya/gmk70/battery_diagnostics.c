// SPDX-License-Identifier: GPL-2.0-or-later
#include "battery_diagnostics.h"
#include "config.h"
#include "battery_output_probe.h"
#include "stock_keycodes.h"
#include "timer.h"
#include <string.h>

// Volatile diagnostic history, limited to the three battery/device test codes.
// Getters never clear it or touch EEPROM; power loss clears it naturally.
static struct {
    battery_diag_snapshot_t press;
    uint16_t                keycode;
    battery_diag_stage_t    stage;
    uint32_t                press_time;
    uint32_t                presses;
    uint32_t                releases;
    uint32_t                indicators;
    uint32_t                level_draws;
    uint32_t                charging_draws;
    uint32_t                last_level_draw_time;
    uint32_t                frames;
    uint32_t                overlay_frames;
    uint32_t                last_overlay_time;
    uint8_t                 last_overlay;
    uint8_t                 before[3];
    uint8_t                 after[3];
    uint8_t                 current[3];
} history;

static void put_u32(uint8_t *data, uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) {
        data[i] = (uint8_t)(value >> (24 - 8 * i));
    }
}

static void encode_snapshot(uint8_t *data, battery_diag_snapshot_t snapshot) {
    put_u32(data, snapshot.layers);
    put_u32(data + 4, snapshot.default_layers);
    data[8]  = snapshot.flags >> 8;
    data[9]  = snapshot.flags;
    data[10] = snapshot.board_device;
    data[11] = snapshot.radio_device;
    data[12] = snapshot.radio_state;
    data[13] = snapshot.battery;
    data[14] = snapshot.peer_battery;
    data[15] = snapshot.rgb_mode;
    data[16] = snapshot.brightness;
    data[17] = snapshot.led_flags;
    data[18] = snapshot.pins;
}

void battery_diagnostics_key(uint16_t keycode, bool pressed, battery_diag_stage_t stage) {
    if (keycode != HS_BATQ && keycode != KC_BATQ && keycode != KC_2G4) {
        return;
    }
    if (stage == BATTERY_DIAG_ENTERED) {
        if (pressed) {
            ++history.presses;
        } else {
            ++history.releases;
        }
    }
    if (pressed) {
        history.keycode    = keycode;
        history.stage      = stage;
        history.press_time = timer_read32();
        history.press      = battery_diagnostics_snapshot();
    }
}

void battery_diagnostics_render(battery_diag_render_t render) {
    switch (render) {
        case BATTERY_DIAG_INDICATOR:
            ++history.indicators;
            break;
        case BATTERY_DIAG_LEVEL_DRAW:
            ++history.level_draws;
            history.last_level_draw_time = timer_read32();
            break;
        case BATTERY_DIAG_CHARGING_DRAW:
            ++history.charging_draws;
            break;
    }
}

void battery_diagnostics_frame(uint8_t rendered, uint8_t before_r, uint8_t before_g, uint8_t before_b, uint8_t after_r, uint8_t after_g, uint8_t after_b) {
    ++history.frames;
    history.current[0] = after_r;
    history.current[1] = after_g;
    history.current[2] = after_b;
    if (rendered) {
        ++history.overlay_frames;
        history.last_overlay_time = timer_read32();
        history.last_overlay      = rendered;
        history.before[0]         = before_r;
        history.before[1]         = before_g;
        history.before[2]         = before_b;
        memcpy(history.after, history.current, sizeof(history.after));
    }
}

bool battery_diagnostics_command(uint8_t *data, uint8_t length) {
    // Private VIA keyboard-value getter: 02 80 <page>, followed by zeros.
    if (length != 32 || data[0] != 0x02 || data[1] != 0x80 || data[2] > 7) {
        return false;
    }
    for (uint8_t i = 3; i < length; ++i) {
        if (data[i] != 0) {
            return false;
        }
    }
    memset(data + 3, 0, length - 3);
    data[3] = 'G';
    data[4] = 'B';
    data[5] = 1; // Protocol version; all multi-byte integers are big endian.
    switch (data[2]) {
        case 0:
            put_u32(data + 6, timer_read32());
            encode_snapshot(data + 10, battery_diagnostics_snapshot());
            break;
        case 1:
            data[6] = history.keycode >> 8;
            data[7] = history.keycode;
            data[8] = history.stage;
            put_u32(data + 9, history.press_time);
            encode_snapshot(data + 13, history.press);
            break;
        case 2:
            put_u32(data + 6, history.presses);
            put_u32(data + 10, history.releases);
            put_u32(data + 14, history.indicators);
            put_u32(data + 18, history.level_draws);
            put_u32(data + 22, history.charging_draws);
            put_u32(data + 26, history.last_level_draw_time);
            data[30] = 3; // Bit 0: frame page 3. Bit 1: output probe pages 4-7.
#ifdef WS2812_SPI_SYNC
            data[30] |= 4; // Bit 2: synchronous LED transmission.
#endif
            break;
        case 3:
            put_u32(data + 6, history.frames);
            put_u32(data + 10, history.overlay_frames);
            put_u32(data + 14, history.last_overlay_time);
            data[18] = history.last_overlay;
            memcpy(data + 19, history.before, sizeof(history.before));
            memcpy(data + 22, history.after, sizeof(history.after));
            memcpy(data + 25, history.current, sizeof(history.current));
            break;
        default: {
            battery_output_sample_t sample = battery_output_probe_sample(data[2] - 4);
            put_u32(data + 6, sample.frames);
            put_u32(data + 10, sample.busy_starts);
            put_u32(data + 14, sample.sampled_at);
            data[18] = sample.packet_size >> 8;
            data[19] = sample.packet_size;
            memcpy(data + 20, sample.led5, sizeof(sample.led5));
            memcpy(data + 23, sample.led33, sizeof(sample.led33));
            data[26] = sample.valid;
            data[27] = sample.uniform_leds;
            data[28] = sample.previous_tx_remaining >> 8;
            data[29] = sample.previous_tx_remaining;
            data[30] = sample.previous_rx_remaining >> 8;
            data[31] = sample.previous_rx_remaining;
            break;
        }
    }
    return true;
}

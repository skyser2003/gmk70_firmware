// SPDX-License-Identifier: GPL-2.0-or-later
#include "battery_output_probe.h"
#include "config.h"
#include <string.h>

enum { LEFT_LEDS = 36, PREAMBLE = 4, PIXEL_BYTES = 12, CHANNEL_BYTES = 4 };

static struct {
    bool                    active;
    battery_output_sample_t sample;
} probe;

void battery_output_probe_frame(bool charging_overlay, bool left) {
    probe.active = charging_overlay && left;
}

static bool decode_pixel(const uint8_t *data, uint8_t rgb[3]) {
    // GMK70 SPI encoding: four bytes/channel, RGB, nibble 8=0 / E=1.
    bool valid = true;
    for (uint8_t channel = 0; channel < 3; ++channel) {
        uint8_t value = 0;
        for (uint8_t i = 0; i < CHANNEL_BYTES; ++i) {
            uint8_t byte = data[CHANNEL_BYTES * channel + i];
            uint8_t high = byte >> 4, low = byte & 15;
            valid = valid && (high == 8 || high == 14) && (low == 8 || low == 14);
            value = (value << 2) | ((high == 14) << 1) | (low == 14);
        }
        rgb[channel] = value;
    }
    return valid;
}

static uint16_t bounded_size(size_t size) {
    return size > UINT16_MAX ? UINT16_MAX : size;
}

void battery_output_probe_transfer(const uint8_t *data, size_t length, bool ready, size_t tx_remaining, size_t rx_remaining, uint32_t now) {
    if (!probe.active) return;
    battery_output_sample_t *sample = &probe.sample;
    ++sample->frames;
    sample->busy_starts += !ready;
    sample->sampled_at            = now;
    sample->packet_size           = bounded_size(length);
    sample->previous_tx_remaining = bounded_size(tx_remaining);
    sample->previous_rx_remaining = bounded_size(rx_remaining);
    sample->valid                 = false;
    sample->uniform_leds          = 0;
    memset(sample->led5, 0, sizeof(sample->led5));
    memset(sample->led33, 0, sizeof(sample->led33));
    if (length < PREAMBLE + LEFT_LEDS * PIXEL_BYTES) return;
    bool first_valid = decode_pixel(data + PREAMBLE + 5 * PIXEL_BYTES, sample->led5);
    sample->valid    = decode_pixel(data + PREAMBLE + HS_MATRIX_BLINK_INDEX_BAT * PIXEL_BYTES, sample->led33) && first_valid;
    for (uint8_t i = 0; i < LEFT_LEDS; ++i) {
        if (memcmp(data + PREAMBLE, data + PREAMBLE + i * PIXEL_BYTES, PIXEL_BYTES) == 0) ++sample->uniform_leds;
    }
}

battery_output_sample_t battery_output_probe_sample(uint8_t phase) {
    // Retain zero-filled pages for the retired optical stages (phases 1-3).
    return phase == 0 ? probe.sample : (battery_output_sample_t){0};
}

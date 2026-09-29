// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t frames;
    uint32_t busy_starts;
    uint32_t sampled_at;
    uint16_t packet_size;
    uint8_t  led5[3];
    uint8_t  led33[3];
    bool     valid;
    uint8_t  uniform_leds;
    uint16_t previous_tx_remaining;
    uint16_t previous_rx_remaining;
} battery_output_sample_t;

// Observe charging output only; never change the rendered colors.
void                    battery_output_probe_frame(bool charging_overlay, bool left);
void                    battery_output_probe_transfer(const uint8_t *data, size_t length, bool ready, size_t tx_remaining, size_t rx_remaining, uint32_t now);
battery_output_sample_t battery_output_probe_sample(uint8_t phase);

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "protocol.h"

enum { GMK_TRIAL_NAMESPACE = 0x71, GMK_TRIAL_ABI = 2, GMK_TRIAL_RUN = 0x10, GMK_TRIAL_WATCHDOG = 0x11, GMK_TRIAL_RESULT = 0x12, GMK_TRIAL_RAM_BASE = 0x20000040, GMK_TRIAL_RAM_END = 0x20006FC0 };
// Runtime intent; the early-startup backend uses a separate retained record.
typedef struct {
    gmk_image_t image;
    bool        armed;
    bool        consumed;
} gmk_trial_launch_t;

bool                gmk_trial_packet_valid(const uint8_t request[GMK_PACKET_SIZE]);
bool                gmk_trial_vectors(const uint8_t *data, uint32_t size);
gmk_update_result_t gmk_trial_buffer_data(gmk_update_t *update, const uint8_t request[GMK_PACKET_SIZE]);
gmk_update_result_t gmk_trial_process(gmk_update_t *update, gmk_trial_launch_t *launch, const uint8_t request[GMK_PACKET_SIZE], bool recovery_ready);
// Consume BEFORE validation/any future platform handoff. Never calls a CPU entry.
bool gmk_trial_take_launch(gmk_trial_launch_t *launch, gmk_image_t *image);

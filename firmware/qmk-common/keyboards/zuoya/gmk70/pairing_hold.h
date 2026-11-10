// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quantum.h"

typedef bool (*gmk70_pairing_device_allowed_t)(uint8_t device);

bool gmk70_pairing_process_record(uint16_t keycode, keyrecord_t *record, gmk70_pairing_device_allowed_t device_allowed);
void gmk70_pairing_release_position(uint8_t row, uint8_t col);
void gmk70_pairing_cancel(void);

// Copyright 2024 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

void __wrap_raw_hid_send(uint8_t *data, uint8_t length);
void __real_raw_hid_send(uint8_t *data, uint8_t length);

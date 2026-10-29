/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool gmk_radio_selected(void);
int  gmk_radio_submit_keyboard(const uint8_t report[8]);
int  gmk_radio_submit_consumer(uint16_t usage);
void gmk_radio_release(bool consumer);
void gmk_radio_clear_hid(void);

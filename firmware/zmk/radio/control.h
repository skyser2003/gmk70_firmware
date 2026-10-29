/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../update/receiver.h"
bool gmk_radio_control_packet(const uint8_t request[32]);
void gmk_radio_control_receive(const uint8_t request[32], uint8_t response[32], bool usb, const gmk_zmk_update_env_t *env);

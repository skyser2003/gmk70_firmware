// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "protocol.h"

enum { GMK_EEPROM_ABI = 1, GMK_EEPROM_LAYOUT = 15 };

// Call from normal housekeeping only, never from the split transport callback.
void gmk_eeprom_query(const uint8_t request[GMK_PACKET_SIZE], gmk_update_result_t result, uint8_t response[GMK_PACKET_SIZE]);

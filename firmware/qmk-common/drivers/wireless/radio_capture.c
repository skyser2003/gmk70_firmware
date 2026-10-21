// SPDX-License-Identifier: GPL-2.0-or-later
#include "radio_capture.h"
#include "module.h"
#include "ch.h"
#include <string.h>

_Static_assert(GMK_RADIO_COUNTERS == 12, "Update the radio snapshot protocol when adding counters");

static uint32_t counters[2][GMK_RADIO_COUNTERS]; // Total and Bluetooth-only; never store payloads.
static uint8_t  version, last_bt_device, max_raw_length;
static bool     version_seen, snapshot_valid;
static uint16_t snapshot_token;
static uint32_t snapshot_words[30];

void gmk_radio_diag_init(void) {
    chSysLock();
    memset(counters, 0, sizeof(counters));
    version = last_bt_device = max_raw_length = 0;
    version_seen = snapshot_valid = false;
    chSysUnlock();
}

void gmk_radio_diag_count(uint8_t counter, uint8_t device) {
    if (counter >= GMK_RADIO_COUNTERS) return;
    chSysLock();
    ++counters[0][counter];
    if (device >= DEVS_BT1 && device <= DEVS_BT5) {
        ++counters[1][counter];
        last_bt_device = device;
    }
    chSysUnlock();
}

void gmk_radio_diag_header(uint8_t length, uint8_t device) {
    gmk_radio_diag_count(GMK_RADIO_RAW_HEADERS, device);
    chSysLock();
    if (length > max_raw_length) max_raw_length = length;
    chSysUnlock();
}

void gmk_radio_diag_version(uint8_t value) {
    chSysLock();
    version      = value;
    version_seen = true;
    chSysUnlock();
}

static void write_word(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = value >> (8 * i);
}

bool gmk_radio_diag_query(uint8_t *packet, uint8_t length, bool allowed, uint32_t uptime) {
    if (length != 32 || packet[0] != 0x22 || packet[1] != 'R' || packet[2] != 'D') return false;
    const uint8_t  page   = packet[4];
    const uint16_t token  = packet[6] | (uint16_t)packet[7] << 8;
    uint8_t        status = !allowed ? 1 : packet[3] != 1 ? 3 : page >= 5 ? 2 : 0;
    memset(packet + 8, 0, 24);
    if (!status) {
        chSysLock();
        if (page == 0) {
            snapshot_words[0] = uptime;
            snapshot_words[1] = version_seen;
            snapshot_words[2] = version;
            snapshot_words[3] = last_bt_device;
            snapshot_words[4] = max_raw_length;
            snapshot_words[5] = 2;
            memcpy(snapshot_words + 6, counters, sizeof(counters));
            snapshot_token = token;
            snapshot_valid = true;
        }
        if (!snapshot_valid || token != snapshot_token) {
            status = 4;
        } else {
            for (unsigned i = 0; i < 6; ++i)
                write_word(packet + 8 + i * 4, snapshot_words[page * 6 + i]);
        }
        chSysUnlock();
    }
    packet[5] = status;
    return true;
}

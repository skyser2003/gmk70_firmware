// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quantum.h"
#include "wireless.h"

typedef struct {
    bool        master;
    uint8_t     state;
    uint8_t     indicator;
    transport_t transport;
    uint8_t     keyboard[MD_SND_CMD_KB_LEN];
    uint8_t     nkro[MD_SND_CMD_NKRO_LEN];
    uint8_t     mouse[MD_SND_CMD_MOUSE_LEN];
    uint16_t    consumer;
    uint8_t     system;
    uint8_t     selected_device;
    bool        pairing;
    unsigned    usb_raw_count;
    unsigned    radio_raw_count;
    unsigned    receive_raw_count;
} radio_fixture_t;

extern radio_fixture_t radio_fixture;
extern host_driver_t   wireless_driver;

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { GMK_TRIAL_PEER_ACK_MS = 300, GMK_TRIAL_PEER_PAUSE_MS = 8000 };

void gmk_trial_peer_init(void);
// Notify only for the initial right-side RUN/PROBE relay. Active pauses never
// extend, so repeated launch requests cannot postpone ordinary split recovery.
void gmk_trial_peer_start(uint32_t now);
bool gmk_trial_peer_active(uint32_t now);
// Updater RPC may collect the launch acknowledgement before the peer resets.
// Ordinary RPC remains suppressed for the entire pause.
bool gmk_trial_peer_rpc_allowed(uint32_t now, bool updater_rpc);

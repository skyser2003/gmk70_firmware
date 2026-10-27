// SPDX-License-Identifier: GPL-2.0-or-later
#include "trial_peer.h"

static bool     peer_paused;
static uint32_t peer_started;

void gmk_trial_peer_init(void) {
    peer_paused  = false;
    peer_started = 0;
}

void gmk_trial_peer_start(uint32_t now) {
    if (gmk_trial_peer_active(now)) return;
    peer_started = now;
    peer_paused  = true;
}

bool gmk_trial_peer_active(uint32_t now) {
    if (peer_paused && (uint32_t)(now - peer_started) >= GMK_TRIAL_PEER_PAUSE_MS) peer_paused = false;
    return peer_paused;
}

bool gmk_trial_peer_rpc_allowed(uint32_t now, bool updater_rpc) {
    if (!gmk_trial_peer_active(now)) return true;
    return updater_rpc && (uint32_t)(now - peer_started) < GMK_TRIAL_PEER_ACK_MS;
}

#ifdef GMK70_VIAL_TRIAL
#    include "split_util.h"
#    include "transactions.h"
#    include "timer.h"

bool __real_transport_master_if_connected(matrix_row_t master_matrix[], matrix_row_t slave_matrix[]);
bool __real_transaction_rpc_exec(int8_t transaction_id, uint8_t request_length, const void *request, uint8_t response_length, void *response);

bool __wrap_transport_master_if_connected(matrix_row_t master_matrix[], matrix_row_t slave_matrix[]) {
    // matrix_post_scan releases the peer's held keys on false. Bypass the
    // upstream function entirely so intentional silence never increments its
    // private connection error counter or starts its disconnected throttle.
    if (gmk_trial_peer_active(timer_read32())) return false;
    return __real_transport_master_if_connected(master_matrix, slave_matrix);
}

bool __wrap_transaction_rpc_exec(int8_t transaction_id, uint8_t request_length, const void *request, uint8_t response_length, void *response) {
    if (!gmk_trial_peer_rpc_allowed(timer_read32(), transaction_id == GMK_UPDATE_RPC)) return false;
    return __real_transaction_rpc_exec(transaction_id, request_length, request, response_length, response);
}
#endif

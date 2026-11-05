/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "core.h"
#include "protocol.h"

enum gmk_power_role { GMK_POWER_ROLE_LEFT = 0, GMK_POWER_ROLE_RIGHT = 1 };
struct gmk_power_platform;
/* All callbacks run on the one service owner. A successful peer exchange must
 * encode/decode through protocol.h and bind its reply to the exact command.
 * Wake preparation preserves ordinary peer UART operation until final pause. */
struct gmk_power_ops {
    struct gmk_power_platform *context;
    bool (*claim_storage)(struct gmk_power_platform *);
    void (*release_storage)(struct gmk_power_platform *);
    bool (*quiesce)(struct gmk_power_platform *, bool enabled);
    bool (*prepare_wake)(struct gmk_power_platform *);
    void (*cancel_wake)(struct gmk_power_platform *);
    bool (*wake_pending)(struct gmk_power_platform *);
    bool (*peer_exchange)(struct gmk_power_platform *, const struct gmk_power_command *, struct gmk_power_reply *);
};
/* No callbacks, packets, retained buffers or hardware ownership live here. */
struct gmk_power_coordinator {
    uint32_t             started_at, last_action, committed_at;
    enum gmk_power_role  role;
    enum gmk_power_phase phase;
    uint16_t             session;
    uint8_t              stage, retries;
    bool                 critical, have_peer, claimed, quiesced, armed, peer_prepared, have_action;
};
bool gmk_power_coordinator_init(struct gmk_power_coordinator *coordinator, enum gmk_power_role role);
bool gmk_power_coordinator_request(struct gmk_power_coordinator *coordinator, enum gmk_power_request reason, bool have_peer, uint32_t now);
void gmk_power_coordinator_step(struct gmk_power_coordinator *coordinator, uint32_t now, bool allowed, bool busy, const struct gmk_power_ops *ops);
/* RIGHT mailbox only. Matching PREPARE/COMMIT/CANCEL commands are idempotent;
 * no command waits for resources or performs a peer exchange. */
bool gmk_power_coordinator_command(struct gmk_power_coordinator *coordinator, const struct gmk_power_command *command, uint32_t now, const struct gmk_power_ops *ops);
void gmk_power_coordinator_cancel(struct gmk_power_coordinator *coordinator, const struct gmk_power_ops *ops);
/* READY admits LEFT or an explicitly standalone role. Joined RIGHT requires a
 * matching COMMIT and 250ms for LEFT to observe it through STATUS before entry. */
bool gmk_power_coordinator_ready(const struct gmk_power_coordinator *coordinator);
bool gmk_power_coordinator_committed(const struct gmk_power_coordinator *coordinator, uint32_t now);

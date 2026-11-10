/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "coordinator.h"

enum { TRANSACTION_MS = 2000, POLL_MS = 30, COMMIT_GRACE_MS = 250 };
enum stage { PROBE, PREPARE, WAIT_READY, COMMIT, WAIT_COMMITTED, COMPLETE };
_Static_assert(sizeof(struct gmk_power_coordinator) < 40, "Power coordinator exceeds admitted state budget");

bool gmk_power_coordinator_init(struct gmk_power_coordinator *coordinator, enum gmk_power_role role) {
    if ((unsigned)role > GMK_POWER_ROLE_RIGHT) return false;
    *coordinator = (struct gmk_power_coordinator){.role = role};
    return true;
}
bool gmk_power_coordinator_request(struct gmk_power_coordinator *coordinator, enum gmk_power_request reason, bool have_peer, uint32_t now) {
    if ((unsigned)coordinator->role > GMK_POWER_ROLE_RIGHT || reason == GMK_POWER_NONE || (unsigned)reason > GMK_POWER_CRITICAL || coordinator->phase != GMK_POWER_PHASE_IDLE || (coordinator->role == GMK_POWER_ROLE_RIGHT && have_peer)) return false;
    uint16_t            session = coordinator->session == UINT16_MAX ? 1 : (uint16_t)(coordinator->session + 1U);
    enum gmk_power_role role    = coordinator->role;
    *coordinator                = (struct gmk_power_coordinator){.role = role, .phase = GMK_POWER_PHASE_REQUESTED, .session = session, .critical = reason == GMK_POWER_CRITICAL, .have_peer = have_peer, .started_at = now};
    return true;
}
static void cancel(struct gmk_power_coordinator *coordinator, const struct gmk_power_ops *ops, bool notify_peer) {
    bool                           notify  = notify_peer && coordinator->role == GMK_POWER_ROLE_LEFT && coordinator->have_peer && coordinator->peer_prepared;
    const struct gmk_power_command command = {.operation = GMK_POWER_CANCEL, .session = coordinator->session, .critical = coordinator->critical};
    /* Restore report admission before reopening scanning. A higher-priority
     * scanner can otherwise deliver the wake key while the radio is paused. */
    if (coordinator->quiesced) {
        (void)ops->quiesce(ops->context, false);
        coordinator->quiesced = false;
    }
    if (coordinator->armed) {
        ops->cancel_wake(ops->context);
        coordinator->armed = false;
    }
    if (coordinator->claimed) {
        ops->release_storage(ops->context);
        coordinator->claimed = false;
    }
    coordinator->phase     = GMK_POWER_PHASE_IDLE;
    coordinator->have_peer = coordinator->peer_prepared = coordinator->have_action = false;
    if (notify) {
        struct gmk_power_reply reply;
        (void)ops->peer_exchange(ops->context, &command, &reply);
    }
}
void gmk_power_coordinator_cancel(struct gmk_power_coordinator *coordinator, const struct gmk_power_ops *ops) {
    cancel(coordinator, ops, true);
}
static bool prepare(struct gmk_power_coordinator *coordinator, const struct gmk_power_ops *ops) {
    if (!coordinator->claimed) {
        if (!ops->claim_storage(ops->context)) goto fail;
        coordinator->claimed = true;
        if (ops->wake_pending(ops->context)) goto fail;
    }
    if (!coordinator->quiesced) {
        /* A failed callback may have partially paused its peripherals. */
        coordinator->quiesced = true;
        if (!ops->quiesce(ops->context, true) || ops->wake_pending(ops->context)) goto fail;
    }
    if (!coordinator->armed) {
        coordinator->armed = true;
        if (!ops->prepare_wake(ops->context) || ops->wake_pending(ops->context)) goto fail;
    }
    return true;
fail:
    cancel(coordinator, ops, true);
    return false;
}
static bool poll_due(struct gmk_power_coordinator *coordinator, uint32_t now) {
    if (coordinator->have_action && (uint32_t)(now - coordinator->last_action) < POLL_MS) return false;
    coordinator->have_action = true;
    coordinator->last_action = now;
    return true;
}
static void peer_step(struct gmk_power_coordinator *coordinator, uint32_t now, const struct gmk_power_ops *ops) {
    if (!poll_due(coordinator, now)) return;
    enum gmk_power_operation operation;
    switch (coordinator->stage) {
        case PROBE:
            operation = GMK_POWER_PROBE;
            break;
        case PREPARE:
            coordinator->peer_prepared = true;
            operation                  = GMK_POWER_PREPARE;
            break;
        case COMMIT:
            /* Set this before attempting delivery. An uncertain COMMIT can
             * only be resolved through STATUS or cancelled, never resent. */
            coordinator->stage = WAIT_COMMITTED;
            operation          = GMK_POWER_COMMIT;
            break;
        case WAIT_READY:
        case WAIT_COMMITTED:
            operation = GMK_POWER_STATUS;
            break;
        default:
            cancel(coordinator, ops, true);
            return;
    }
    const struct gmk_power_command command = {.operation = operation, .session = coordinator->session, .critical = coordinator->critical};
    struct gmk_power_reply         reply;
    bool                           received = ops->peer_exchange(ops->context, &command, &reply);
    if (ops->wake_pending(ops->context)) {
        cancel(coordinator, ops, true);
        return;
    }
    if (!received) {
        if (coordinator->retries < UINT8_MAX) ++coordinator->retries;
        return;
    }
    if (reply.flags || (unsigned)reply.phase > GMK_POWER_PHASE_COMMITTED) {
        cancel(coordinator, ops, true);
        return;
    }
    if (operation == GMK_POWER_PROBE) {
        if (reply.phase != GMK_POWER_PHASE_IDLE) {
            cancel(coordinator, ops, true);
            return;
        }
        coordinator->stage = PREPARE;
    } else if (operation == GMK_POWER_PREPARE || coordinator->stage == WAIT_READY) {
        if (reply.phase == GMK_POWER_PHASE_COMMITTED) {
            cancel(coordinator, ops, true);
            return;
        }
        coordinator->stage = reply.phase == GMK_POWER_PHASE_READY ? COMMIT : WAIT_READY;
    } else if (operation == GMK_POWER_STATUS && reply.phase == GMK_POWER_PHASE_COMMITTED) {
        coordinator->phase = GMK_POWER_PHASE_READY;
        coordinator->stage = COMPLETE;
    }
}
/* Preserve the linked safety boundary under LTO. */
__attribute__((noipa)) void gmk_power_coordinator_step(struct gmk_power_coordinator *coordinator, uint32_t now, bool allowed, bool busy, const struct gmk_power_ops *ops) {
    if (coordinator->phase == GMK_POWER_PHASE_IDLE) return;
    if (!allowed || busy || (uint32_t)(now - coordinator->started_at) >= TRANSACTION_MS || ops->wake_pending(ops->context)) {
        cancel(coordinator, ops, true);
        return;
    }
    if (!prepare(coordinator, ops)) return;
    if (coordinator->phase == GMK_POWER_PHASE_COMMITTED || coordinator->phase == GMK_POWER_PHASE_READY) return;
    if (!coordinator->have_peer || coordinator->role == GMK_POWER_ROLE_RIGHT) {
        coordinator->phase = GMK_POWER_PHASE_READY;
        return;
    }
    peer_step(coordinator, now, ops);
}
bool gmk_power_coordinator_command(struct gmk_power_coordinator *coordinator, const struct gmk_power_command *command, uint32_t now, const struct gmk_power_ops *ops) {
    if (coordinator->role != GMK_POWER_ROLE_RIGHT || (unsigned)command->operation > GMK_POWER_CANCEL || !command->session) return false;
    if (command->operation == GMK_POWER_PROBE) return true;
    bool matches = command->session == coordinator->session && command->critical == coordinator->critical;
    if (command->operation == GMK_POWER_PREPARE) {
        if (coordinator->phase != GMK_POWER_PHASE_IDLE) return matches && coordinator->have_peer;
        if (command->session == coordinator->session) return false;
        *coordinator = (struct gmk_power_coordinator){.role = GMK_POWER_ROLE_RIGHT, .phase = GMK_POWER_PHASE_REQUESTED, .session = command->session, .critical = command->critical, .have_peer = true, .started_at = now};
        return true;
    }
    if (!matches) return false;
    if (command->operation == GMK_POWER_STATUS) return true;
    if (command->operation == GMK_POWER_CANCEL) {
        cancel(coordinator, ops, false);
        return true;
    }
    if (!coordinator->have_peer || (coordinator->phase != GMK_POWER_PHASE_READY && coordinator->phase != GMK_POWER_PHASE_COMMITTED)) return false;
    if (ops->wake_pending(ops->context)) {
        cancel(coordinator, ops, false);
        return false;
    }
    if (coordinator->phase == GMK_POWER_PHASE_READY) {
        coordinator->phase        = GMK_POWER_PHASE_COMMITTED;
        coordinator->committed_at = coordinator->started_at = now;
    }
    return true;
}
bool gmk_power_coordinator_ready(const struct gmk_power_coordinator *coordinator) {
    return coordinator->phase == GMK_POWER_PHASE_READY && (coordinator->role == GMK_POWER_ROLE_LEFT || !coordinator->have_peer);
}
bool gmk_power_coordinator_committed(const struct gmk_power_coordinator *coordinator, uint32_t now) {
    return coordinator->role == GMK_POWER_ROLE_RIGHT && coordinator->have_peer && coordinator->phase == GMK_POWER_PHASE_COMMITTED && (uint32_t)(now - coordinator->committed_at) >= COMMIT_GRACE_MS;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "coordinator.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum action { CLAIM, QUIESCE, ARM, RESUME, DISARM, RELEASE, EXCHANGE };
struct gmk_power_platform {
    struct gmk_power_coordinator *owner;
    struct gmk_power_platform    *peer;
    uint32_t                      now;
    enum action                   actions[128];
    unsigned                      count, messages[5], drop_prepare_replies;
    bool                          claimed, quiesced, armed, wake;
    bool                          claim_ok, quiesce_ok, arm_ok, compatible;
    bool                          corrupt_reply, stale_reply, drop_commit_reply, drop_commit_command, cached_reply;
    bool                          inject_wake;
    enum action                   wake_action;
    uint8_t                       reply_flags;
};
static struct gmk_power_ops operations(struct gmk_power_platform *platform);
static void                 record(struct gmk_power_platform *platform, enum action action) {
    assert(platform->count < sizeof(platform->actions) / sizeof(platform->actions[0]));
    platform->actions[platform->count++] = action;
    if (platform->inject_wake && platform->wake_action == action) platform->wake = true;
}
static bool claim_storage(struct gmk_power_platform *platform) {
    record(platform, CLAIM);
    assert(!platform->claimed && !platform->quiesced && !platform->armed);
    if (platform->claim_ok) platform->claimed = true;
    return platform->claim_ok;
}
static void release_storage(struct gmk_power_platform *platform) {
    record(platform, RELEASE);
    assert(platform->claimed && !platform->quiesced && !platform->armed);
    platform->claimed = false;
}
static bool quiesce(struct gmk_power_platform *platform, bool enabled) {
    record(platform, enabled ? QUIESCE : RESUME);
    assert(platform->claimed && (!enabled || !platform->armed));
    platform->quiesced = enabled; /* Even a failed preparation can need unwind. */
    return !enabled || platform->quiesce_ok;
}
static bool prepare_wake(struct gmk_power_platform *platform) {
    record(platform, ARM);
    assert(platform->claimed && platform->quiesced);
    platform->armed = true;
    return platform->arm_ok;
}
static void cancel_wake(struct gmk_power_platform *platform) {
    record(platform, DISARM);
    assert(platform->claimed && !platform->quiesced && platform->armed);
    platform->armed = false;
}
static bool wake_pending(struct gmk_power_platform *platform) {
    return platform->wake;
}
static bool peer_exchange(struct gmk_power_platform *platform, const struct gmk_power_command *command, struct gmk_power_reply *reply) {
    record(platform, EXCHANGE);
    assert((unsigned)command->operation <= GMK_POWER_CANCEL);
    ++platform->messages[command->operation];
    uint8_t request[32], response[32] = {0};
    assert(gmk_power_protocol_request_encode(request, command));
    if (!platform->compatible) return gmk_power_protocol_reply_decode(request, response, reply);
    assert(platform->peer);
    if (command->operation == GMK_POWER_COMMIT && platform->drop_commit_command) return false;
    struct gmk_power_command decoded;
    assert(gmk_power_protocol_request_decode(request, &decoded));
    struct gmk_power_platform *peer     = platform->peer;
    enum gmk_power_phase       prior    = peer->owner->phase;
    struct gmk_power_ops       peer_ops = operations(peer);
    if (!gmk_power_coordinator_command(peer->owner, &decoded, platform->now, &peer_ops)) return false;
    struct gmk_power_reply status = {.phase = platform->cached_reply ? prior : peer->owner->phase, .flags = platform->reply_flags};
    assert(gmk_power_protocol_reply_encode(response, request, &status));
    if (command->operation == GMK_POWER_PREPARE && platform->drop_prepare_replies) {
        --platform->drop_prepare_replies;
        return false;
    }
    if (command->operation == GMK_POWER_COMMIT && platform->drop_commit_reply) return false;
    if (platform->corrupt_reply) response[31] ^= 1;
    if (platform->stale_reply) {
        response[6] ^= 1;
        /* Semantic mismatch despite a correct wire checksum. */
        struct gmk_power_command changed = *command;
        changed.session ^= 1;
        if (!changed.session) changed.session = 2;
        uint8_t changed_request[32];
        assert(gmk_power_protocol_request_encode(changed_request, &changed));
        assert(gmk_power_protocol_reply_encode(response, changed_request, &status));
    }
    return gmk_power_protocol_reply_decode(request, response, reply);
}
static struct gmk_power_ops operations(struct gmk_power_platform *platform) {
    return (struct gmk_power_ops){.context = platform, .claim_storage = claim_storage, .release_storage = release_storage, .quiesce = quiesce, .prepare_wake = prepare_wake, .cancel_wake = cancel_wake, .wake_pending = wake_pending, .peer_exchange = peer_exchange};
}
static void initialize(struct gmk_power_coordinator *coordinator, struct gmk_power_platform *platform, enum gmk_power_role role) {
    assert(gmk_power_coordinator_init(coordinator, role));
    *platform = (struct gmk_power_platform){.owner = coordinator, .claim_ok = true, .quiesce_ok = true, .arm_ok = true, .compatible = true};
}
static void step(struct gmk_power_platform *platform, uint32_t now, bool allowed, bool busy) {
    platform->now            = now;
    struct gmk_power_ops ops = operations(platform);
    gmk_power_coordinator_step(platform->owner, now, allowed, busy, &ops);
}
static void assert_unwound(const struct gmk_power_platform *platform) {
    assert(platform->owner->phase == GMK_POWER_PHASE_IDLE);
    assert(!platform->claimed && !platform->quiesced && !platform->armed);
    assert(!platform->owner->claimed && !platform->owner->quiesced && !platform->owner->armed);
}
static void standalone_restores_reports_before_reopening_scanning(void) {
    for (unsigned role = 0; role <= 1; ++role) {
        struct gmk_power_coordinator coordinator;
        struct gmk_power_platform    platform;
        initialize(&coordinator, &platform, (enum gmk_power_role)role);
        assert(gmk_power_coordinator_request(&coordinator, GMK_POWER_INACTIVITY, false, 10));
        step(&platform, 10, true, false);
        assert(gmk_power_coordinator_ready(&coordinator));
        assert(platform.count == 3 && platform.actions[0] == CLAIM && platform.actions[1] == QUIESCE && platform.actions[2] == ARM);
        struct gmk_power_ops ops = operations(&platform);
        gmk_power_coordinator_cancel(&coordinator, &ops);
        assert_unwound(&platform);
        assert(platform.count == 6 && platform.actions[3] == RESUME && platform.actions[4] == DISARM && platform.actions[5] == RELEASE);
        gmk_power_coordinator_cancel(&coordinator, &ops);
        assert(platform.count == 6);
    }
}
static void failed_or_racing_preparation_releases_only_owned_resources(void) {
    for (unsigned failure = 0; failure < 5; ++failure) {
        struct gmk_power_coordinator coordinator;
        struct gmk_power_platform    platform;
        initialize(&coordinator, &platform, GMK_POWER_ROLE_LEFT);
        if (failure == 0) platform.claim_ok = false;
        if (failure == 1) platform.quiesce_ok = false;
        if (failure == 2) platform.arm_ok = false;
        if (failure >= 3) {
            platform.inject_wake = true;
            platform.wake_action = failure == 3 ? CLAIM : ARM;
        }
        assert(gmk_power_coordinator_request(&coordinator, GMK_POWER_INACTIVITY, false, 0));
        step(&platform, 0, true, false);
        assert_unwound(&platform);
        assert(!gmk_power_coordinator_ready(&coordinator));
    }
}
static void right_requires_matching_left_authorization_and_preserves_commit_delay(void) {
    struct gmk_power_coordinator coordinator;
    struct gmk_power_platform    platform;
    initialize(&coordinator, &platform, GMK_POWER_ROLE_RIGHT);
    struct gmk_power_ops ops = operations(&platform);
    assert(!gmk_power_coordinator_request(&coordinator, GMK_POWER_INACTIVITY, true, 0));
    struct gmk_power_command command = {.operation = GMK_POWER_PREPARE, .session = 345, .critical = true};
    assert(gmk_power_coordinator_command(&coordinator, &command, 10, &ops));
    assert(coordinator.phase == GMK_POWER_PHASE_REQUESTED && platform.count == 0);
    command.operation = GMK_POWER_COMMIT;
    assert(!gmk_power_coordinator_command(&coordinator, &command, 11, &ops));
    step(&platform, 20, true, false);
    assert(coordinator.phase == GMK_POWER_PHASE_READY && !gmk_power_coordinator_ready(&coordinator));
    command.session = 346;
    assert(!gmk_power_coordinator_command(&coordinator, &command, 30, &ops));
    command.session  = 345;
    command.critical = false;
    assert(!gmk_power_coordinator_command(&coordinator, &command, 30, &ops));
    command.critical = true;
    assert(gmk_power_coordinator_command(&coordinator, &command, 100, &ops));
    assert(coordinator.phase == GMK_POWER_PHASE_COMMITTED);
    assert(!gmk_power_coordinator_committed(&coordinator, 349));
    assert(gmk_power_coordinator_command(&coordinator, &command, 300, &ops));
    assert(gmk_power_coordinator_committed(&coordinator, 350));
    command.operation = GMK_POWER_PREPARE;
    assert(gmk_power_coordinator_command(&coordinator, &command, 320, &ops));
    assert(gmk_power_coordinator_committed(&coordinator, 350) && platform.count == 3);
    command.operation = GMK_POWER_CANCEL;
    assert(gmk_power_coordinator_command(&coordinator, &command, 351, &ops));
    assert_unwound(&platform);
    assert(gmk_power_coordinator_command(&coordinator, &command, 352, &ops));
}
static void run_pair(struct gmk_power_platform *left, struct gmk_power_platform *right, uint32_t until) {
    for (uint32_t now = 0; now <= until; now += 30) {
        step(left, now, true, false);
        step(right, now, true, false);
    }
}
static void left_probes_prepares_and_commits_once_then_confirms_status(void) {
    for (unsigned uncertainty = 0; uncertainty < 3; ++uncertainty) {
        struct gmk_power_coordinator left_state, right_state;
        struct gmk_power_platform    left, right;
        initialize(&left_state, &left, GMK_POWER_ROLE_LEFT);
        initialize(&right_state, &right, GMK_POWER_ROLE_RIGHT);
        left.peer                 = &right;
        left.drop_commit_reply    = uncertainty == 1;
        left.cached_reply         = uncertainty == 2;
        left.drop_prepare_replies = 1;
        assert(gmk_power_coordinator_request(&left_state, GMK_POWER_CRITICAL, true, 0));
        run_pair(&left, &right, 210);
        assert(gmk_power_coordinator_ready(&left_state));
        assert(right_state.phase == GMK_POWER_PHASE_COMMITTED && right_state.critical);
        assert(left.messages[GMK_POWER_PROBE] == 1 && left.messages[GMK_POWER_PREPARE] == 2 && left.messages[GMK_POWER_COMMIT] == 1);
        assert(left.messages[GMK_POWER_STATUS] >= 1);
        struct gmk_power_ops ops = operations(&left);
        gmk_power_coordinator_cancel(&left_state, &ops);
        assert_unwound(&left);
        assert_unwound(&right);
        assert(left.messages[GMK_POWER_CANCEL] == 1);
    }
}
static void lost_commit_never_retries_mutation_and_timeout_cancels_both_halves(void) {
    struct gmk_power_coordinator left_state, right_state;
    struct gmk_power_platform    left, right;
    initialize(&left_state, &left, GMK_POWER_ROLE_LEFT);
    initialize(&right_state, &right, GMK_POWER_ROLE_RIGHT);
    left.peer                = &right;
    left.drop_commit_command = true;
    assert(gmk_power_coordinator_request(&left_state, GMK_POWER_INACTIVITY, true, 0));
    run_pair(&left, &right, 1980);
    assert(!gmk_power_coordinator_ready(&left_state) && left.messages[GMK_POWER_COMMIT] == 1);
    step(&left, 2000, true, false);
    assert_unwound(&left);
    assert_unwound(&right);
    assert(left.messages[GMK_POWER_CANCEL] == 1);
}
static void incompatible_corrupt_and_stale_probe_replies_never_prepare_peer(void) {
    for (unsigned invalid = 0; invalid < 3; ++invalid) {
        struct gmk_power_coordinator left_state, right_state;
        struct gmk_power_platform    left, right;
        initialize(&left_state, &left, GMK_POWER_ROLE_LEFT);
        initialize(&right_state, &right, GMK_POWER_ROLE_RIGHT);
        left.peer          = &right;
        left.compatible    = invalid != 0;
        left.corrupt_reply = invalid == 1;
        left.stale_reply   = invalid == 2;
        assert(gmk_power_coordinator_request(&left_state, GMK_POWER_INACTIVITY, true, 0));
        run_pair(&left, &right, 1980);
        step(&left, 2000, true, false);
        assert_unwound(&left);
        assert_unwound(&right);
        assert(left.messages[GMK_POWER_PROBE] > 0 && left.messages[GMK_POWER_PREPARE] == 0 && left.messages[GMK_POWER_COMMIT] == 0);
    }
}
static void busy_usb_or_new_input_aborts_before_or_after_resource_acquisition(void) {
    for (unsigned cancellation = 0; cancellation < 6; ++cancellation) {
        struct gmk_power_coordinator state;
        struct gmk_power_platform    platform;
        initialize(&state, &platform, GMK_POWER_ROLE_LEFT);
        assert(gmk_power_coordinator_request(&state, GMK_POWER_INACTIVITY, false, 0));
        if (cancellation >= 3) step(&platform, 0, true, false);
        platform.wake = cancellation % 3 == 2;
        step(&platform, 10, cancellation % 3 != 0, cancellation % 3 == 1);
        assert_unwound(&platform);
    }
}
static void peer_guard_flags_abort_and_new_edges_cancel_an_uncertain_commit(void) {
    const uint8_t guards[] = {GMK_POWER_FLAG_USB, GMK_POWER_FLAG_BUSY, GMK_POWER_FLAG_ACTIVE, 7};
    for (unsigned guard = 0; guard < sizeof(guards); ++guard) {
        struct gmk_power_coordinator left_state, right_state;
        struct gmk_power_platform    left, right;
        initialize(&left_state, &left, GMK_POWER_ROLE_LEFT);
        initialize(&right_state, &right, GMK_POWER_ROLE_RIGHT);
        left.peer        = &right;
        left.reply_flags = guards[guard];
        assert(gmk_power_coordinator_request(&left_state, GMK_POWER_INACTIVITY, true, 0));
        step(&left, 0, true, false);
        assert_unwound(&left);
        assert_unwound(&right);
        assert(left.messages[GMK_POWER_PREPARE] == 0);
    }
    for (unsigned cancellation = 0; cancellation < 2; ++cancellation) {
        struct gmk_power_coordinator left_state, right_state;
        struct gmk_power_platform    left, right;
        initialize(&left_state, &left, GMK_POWER_ROLE_LEFT);
        initialize(&right_state, &right, GMK_POWER_ROLE_RIGHT);
        left.peer = &right;
        assert(gmk_power_coordinator_request(&left_state, GMK_POWER_CRITICAL, true, 0));
        run_pair(&left, &right, 60);
        if (cancellation == 0) {
            left.inject_wake       = true;
            left.wake_action       = EXCHANGE;
            left.drop_commit_reply = true;
        } else {
            left.reply_flags = GMK_POWER_FLAG_ACTIVE;
        }
        step(&left, 90, true, false);
        assert_unwound(&left);
        assert_unwound(&right);
        assert(left.messages[GMK_POWER_COMMIT] == 1 && left.messages[GMK_POWER_CANCEL] == 1);
    }
}
static void early_commit_after_a_new_edge_is_rejected_and_polling_is_bounded(void) {
    struct gmk_power_coordinator right_state;
    struct gmk_power_platform    right;
    initialize(&right_state, &right, GMK_POWER_ROLE_RIGHT);
    struct gmk_power_ops     ops     = operations(&right);
    struct gmk_power_command command = {.operation = GMK_POWER_PREPARE, .session = 1};
    assert(gmk_power_coordinator_command(&right_state, &command, 0, &ops));
    step(&right, 0, true, false);
    right.wake        = true;
    command.operation = GMK_POWER_COMMIT;
    assert(!gmk_power_coordinator_command(&right_state, &command, 30, &ops));
    assert_unwound(&right);
    struct gmk_power_coordinator left_state;
    struct gmk_power_platform    left;
    initialize(&left_state, &left, GMK_POWER_ROLE_LEFT);
    left.compatible = false;
    assert(gmk_power_coordinator_request(&left_state, GMK_POWER_INACTIVITY, true, 0));
    for (uint32_t now = 0; now < 30; ++now)
        step(&left, now, true, false);
    assert(left.messages[GMK_POWER_PROBE] == 1);
    step(&left, 30, true, false);
    assert(left.messages[GMK_POWER_PROBE] == 2);
}
static void commit_grace_and_closed_session_guards_survive_uptime_wrap(void) {
    struct gmk_power_coordinator coordinator;
    struct gmk_power_platform    platform;
    initialize(&coordinator, &platform, GMK_POWER_ROLE_RIGHT);
    struct gmk_power_ops     ops     = operations(&platform);
    struct gmk_power_command command = {.operation = GMK_POWER_PREPARE, .session = UINT16_MAX};
    assert(gmk_power_coordinator_command(&coordinator, &command, UINT32_MAX - 1000, &ops));
    step(&platform, UINT32_MAX - 1000, true, false);
    command.operation = GMK_POWER_COMMIT;
    assert(gmk_power_coordinator_command(&coordinator, &command, UINT32_MAX - 100, &ops));
    assert(!gmk_power_coordinator_committed(&coordinator, 148));
    assert(gmk_power_coordinator_committed(&coordinator, 149));
    command.operation = GMK_POWER_CANCEL;
    command.session   = 1;
    assert(!gmk_power_coordinator_command(&coordinator, &command, 150, &ops));
    assert(coordinator.phase == GMK_POWER_PHASE_COMMITTED);
    command.session = UINT16_MAX;
    assert(gmk_power_coordinator_command(&coordinator, &command, 150, &ops));
    assert_unwound(&platform);
    command.operation = GMK_POWER_COMMIT;
    assert(!gmk_power_coordinator_command(&coordinator, &command, 151, &ops));
    command.operation = GMK_POWER_PREPARE;
    assert(!gmk_power_coordinator_command(&coordinator, &command, 152, &ops));
    command.session = 1;
    assert(gmk_power_coordinator_command(&coordinator, &command, 153, &ops));
    assert(coordinator.session == 1 && coordinator.phase == GMK_POWER_PHASE_REQUESTED);
}
static void an_abandoned_right_session_times_out_and_late_commit_gets_its_full_grace(void) {
    for (unsigned committed = 0; committed < 2; ++committed) {
        struct gmk_power_coordinator coordinator;
        struct gmk_power_platform    platform;
        initialize(&coordinator, &platform, GMK_POWER_ROLE_RIGHT);
        struct gmk_power_ops     ops     = operations(&platform);
        struct gmk_power_command command = {.operation = GMK_POWER_PREPARE, .session = 55};
        assert(gmk_power_coordinator_command(&coordinator, &command, 0, &ops));
        step(&platform, 0, true, false);
        if (committed) {
            command.operation = GMK_POWER_COMMIT;
            assert(gmk_power_coordinator_command(&coordinator, &command, 1900, &ops));
            step(&platform, 2000, true, false);
            assert(coordinator.phase == GMK_POWER_PHASE_COMMITTED);
            assert(!gmk_power_coordinator_committed(&coordinator, 2149));
            assert(gmk_power_coordinator_committed(&coordinator, 2150));
            step(&platform, 3900, true, false);
        } else {
            step(&platform, 2000, true, false);
        }
        assert_unwound(&platform);
    }
}
static void roles_bounds_sessions_and_wrapping_deadlines_fail_closed(void) {
    struct gmk_power_coordinator state;
    struct gmk_power_platform    platform;
    initialize(&state, &platform, GMK_POWER_ROLE_LEFT);
    assert(sizeof(state) < 40);
    assert(!gmk_power_coordinator_init(&state, (enum gmk_power_role)2));
    assert(!gmk_power_coordinator_request(&state, GMK_POWER_NONE, false, 0));
    assert(!gmk_power_coordinator_request(&state, (enum gmk_power_request)99, false, 0));
    state.session = UINT16_MAX;
    assert(gmk_power_coordinator_request(&state, GMK_POWER_INACTIVITY, false, UINT32_MAX - 1000));
    assert(state.session == 1);
    assert(!gmk_power_coordinator_request(&state, GMK_POWER_CRITICAL, false, 10));
    step(&platform, UINT32_MAX - 1000, true, false);
    assert(gmk_power_coordinator_ready(&state));
    step(&platform, 998, true, false);
    assert(gmk_power_coordinator_ready(&state));
    step(&platform, 999, true, false);
    assert_unwound(&platform);
    struct gmk_power_ops           ops     = operations(&platform);
    const struct gmk_power_command command = {.operation = GMK_POWER_PREPARE, .session = 1};
    assert(!gmk_power_coordinator_command(&state, &command, 1000, &ops));
}
int main(void) {
    standalone_restores_reports_before_reopening_scanning();
    failed_or_racing_preparation_releases_only_owned_resources();
    right_requires_matching_left_authorization_and_preserves_commit_delay();
    left_probes_prepares_and_commits_once_then_confirms_status();
    lost_commit_never_retries_mutation_and_timeout_cancels_both_halves();
    incompatible_corrupt_and_stale_probe_replies_never_prepare_peer();
    busy_usb_or_new_input_aborts_before_or_after_resource_acquisition();
    peer_guard_flags_abort_and_new_edges_cancel_an_uncertain_commit();
    early_commit_after_a_new_edge_is_rejected_and_polling_is_bounded();
    commit_grace_and_closed_session_guards_survive_uptime_wrap();
    an_abandoned_right_session_times_out_and_late_commit_gets_its_full_grace();
    roles_bounds_sessions_and_wrapping_deadlines_fail_closed();
    puts("Power coordinator: ordered leases/wake, CRC-bound peer handshake, single commit and cancellation passed");
    return 0;
}

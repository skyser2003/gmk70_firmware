// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/trial_peer.h"
#include <assert.h>
#include <stdio.h>

static void test_ordinary_traffic_is_suppressed_until_peer_recovery(void) {
    gmk_trial_peer_init();
    assert(!gmk_trial_peer_active(100));
    assert(gmk_trial_peer_rpc_allowed(100, false));
    assert(gmk_trial_peer_rpc_allowed(100, true));

    gmk_trial_peer_start(100);
    assert(gmk_trial_peer_active(100));
    assert(!gmk_trial_peer_rpc_allowed(100, false));
    assert(!gmk_trial_peer_rpc_allowed(8099, false));
    assert(gmk_trial_peer_active(8099));
    assert(!gmk_trial_peer_active(8100));
    assert(gmk_trial_peer_rpc_allowed(8100, false));
    assert(gmk_trial_peer_rpc_allowed(8100, true));
}

static void test_updater_acknowledgements_stop_before_peer_reset(void) {
    gmk_trial_peer_init();
    gmk_trial_peer_start(0);
    assert(gmk_trial_peer_rpc_allowed(0, true));
    assert(gmk_trial_peer_rpc_allowed(299, true));
    assert(!gmk_trial_peer_rpc_allowed(300, true));
    assert(!gmk_trial_peer_rpc_allowed(7999, true));
    assert(gmk_trial_peer_rpc_allowed(8000, true));
}

static void test_repeated_notifications_do_not_extend_the_pause(void) {
    gmk_trial_peer_init();
    gmk_trial_peer_start(10);
    gmk_trial_peer_start(200);
    assert(!gmk_trial_peer_rpc_allowed(310, true));
    gmk_trial_peer_start(8009);
    assert(gmk_trial_peer_active(8009));
    assert(!gmk_trial_peer_active(8010));
    assert(gmk_trial_peer_rpc_allowed(8010, false));

    gmk_trial_peer_start(8010);
    assert(gmk_trial_peer_active(8010));
    assert(gmk_trial_peer_rpc_allowed(8010, true));
    assert(!gmk_trial_peer_rpc_allowed(8310, true));
    assert(!gmk_trial_peer_active(16010));
}

static void test_pause_and_acknowledgement_windows_cross_timer_wrap(void) {
    gmk_trial_peer_init();
    gmk_trial_peer_start(UINT32_MAX - 99);
    assert(gmk_trial_peer_active(UINT32_MAX));
    assert(gmk_trial_peer_rpc_allowed(199, true));
    assert(!gmk_trial_peer_rpc_allowed(200, true));
    assert(!gmk_trial_peer_rpc_allowed(7899, false));
    assert(gmk_trial_peer_active(7899));
    assert(!gmk_trial_peer_active(7900));
    assert(gmk_trial_peer_rpc_allowed(7900, false));
}

static void test_initialization_clears_a_previous_pause(void) {
    gmk_trial_peer_init();
    gmk_trial_peer_start(100);
    gmk_trial_peer_init();
    assert(!gmk_trial_peer_active(101));
    assert(gmk_trial_peer_rpc_allowed(101, false));
    assert(gmk_trial_peer_rpc_allowed(101, true));
}

int main(void) {
    test_ordinary_traffic_is_suppressed_until_peer_recovery();
    test_updater_acknowledgements_stop_before_peer_reset();
    test_repeated_notifications_do_not_extend_the_pause();
    test_pause_and_acknowledgement_windows_cross_timer_wrap();
    test_initialization_clears_a_previous_pause();
    puts("trial peer tests passed");
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/trial_boot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const gmk_image_t image = {.size = 4096, .crc = 0xA63C7195};

static void prepare(gmk_trial_boot_record_t *record, uint32_t action, uint32_t role) {
    memset(record, 0xA5, sizeof(*record));
    assert(gmk_trial_boot_prepare(record, action, image, role));
}

static gmk_trial_boot_result_t report(const gmk_trial_boot_record_t *record, uint32_t phase) {
    gmk_trial_boot_record_t saved = *record;
    gmk_trial_boot_result_t result;
    assert(gmk_trial_boot_report(record, &result));
    assert(memcmp(record, &saved, sizeof(saved)) == 0);
    assert(result.phase == phase);
    assert(result.image.size == 4096 && result.image.crc == 0xA63C7195);
    return result;
}

// A successful consume must remove the launch before flash validation, so a
// reset or another consume cannot execute the same image a second time.
static void test_consumed_before_entry_and_one_shot(void) {
    gmk_trial_boot_record_t record;
    prepare(&record, 1, 0);
    gmk_trial_boot_result_t result = report(&record, 1);
    assert(result.action == 1 && result.role == 0 && result.reset_flags == 0);
    gmk_trial_boot_request_t request = {0};
    assert(gmk_trial_boot_consume(&record, 8, 0, &request));
    assert(request.action == 1 && request.role == 0);
    assert(request.image.size == 4096 && request.image.crc == 0xA63C7195);
    result = report(&record, 4);
    assert(result.reset_flags == 8);
    assert(!gmk_trial_boot_consume(&record, 8, 0, &request));
    assert(gmk_trial_boot_enter(&record));
    result = report(&record, 2);
    assert(result.reset_flags == 8);
    assert(!gmk_trial_boot_enter(&record));
    assert(!gmk_trial_boot_consume(&record, 8, 0, &request));
    assert(gmk_trial_boot_complete(&record, 4));
    result = report(&record, 3);
    assert(result.action == 1 && result.role == 0 && result.reset_flags == 4);
    gmk_trial_boot_record_t saved = record;
    assert(!gmk_trial_boot_consume(&record, 8, 0, &request));
    assert(!gmk_trial_boot_complete(&record, 8));
    assert(memcmp(&record, &saved, sizeof(record)) == 0);
}

// Only the deliberate software reset (with optional companion PINRST) may
// launch. Sticky watchdog, POR, low-power, window-watchdog or unknown flags
// must suppress RUN and consume the intent permanently.
static void test_reset_cause_matrix(void) {
    for (uint32_t flags = 0; flags < 128; ++flags) {
        gmk_trial_boot_record_t record;
        prepare(&record, 1, 0);
        gmk_trial_boot_request_t request  = {.action = 99};
        bool                     accepted = flags == 8 || flags == 40;
        assert(gmk_trial_boot_consume(&record, flags, 0, &request) == accepted);
        gmk_trial_boot_result_t result = report(&record, 4);
        assert(result.reset_flags == flags);
        if (accepted) {
            assert(request.action == 1);
            assert(gmk_trial_boot_enter(&record));
        } else {
            assert(request.action == 99);
            assert(!gmk_trial_boot_enter(&record));
        }
        assert(!gmk_trial_boot_consume(&record, 8, 0, &request));
    }
}

// A correct record from the other half must not execute locally, and rejecting
// it must not leave an intent that can later be retried with another role.
static void test_roles(void) {
    for (uint32_t prepared_role = 0; prepared_role <= 1; ++prepared_role) {
        for (uint32_t actual_role = 0; actual_role <= 2; ++actual_role) {
            gmk_trial_boot_record_t record;
            prepare(&record, 1, prepared_role);
            gmk_trial_boot_request_t request  = {.action = 99};
            bool                     accepted = prepared_role == actual_role;
            assert(gmk_trial_boot_consume(&record, 8, actual_role, &request) == accepted);
            assert(report(&record, 4).role == prepared_role);
            assert(!gmk_trial_boot_consume(&record, 8, prepared_role, &request));
            if (accepted)
                assert(gmk_trial_boot_enter(&record));
            else
                assert(!gmk_trial_boot_enter(&record));
        }
    }
}

// Any changed bit, including metadata, reserved bytes, checksum, complement or
// commit magic, must make the retained record ineligible for launch/report.
static void test_corruption_and_torn_record(void) {
    gmk_trial_boot_record_t valid;
    prepare(&valid, 1, 0);
    for (unsigned byte = 0; byte < sizeof(valid); ++byte) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            gmk_trial_boot_record_t record = valid;
            ((uint8_t *)&record)[byte] ^= 1U << bit;
            gmk_trial_boot_request_t request = {.action = 99};
            gmk_trial_boot_result_t  result;
            assert(!gmk_trial_boot_report(&record, &result));
            bool requested = record.phase == 1;
            assert(!gmk_trial_boot_consume(&record, 8, 0, &request));
            if (requested) assert(record.magic == 0);
            assert(request.action == 99);
            assert(!gmk_trial_boot_enter(&record));
            assert(!gmk_trial_boot_complete(&record, 4));
        }
    }
    // Commit is last: every partial body under invalid magic is inert, even
    // when all other bytes already match the completed request.
    for (unsigned bytes = 0; bytes <= sizeof(valid); ++bytes) {
        gmk_trial_boot_record_t record;
        memset(&record, 0, sizeof(record));
        if (bytes > sizeof(record.magic)) memcpy((uint8_t *)&record + sizeof(record.magic), (const uint8_t *)&valid + sizeof(record.magic), bytes - sizeof(record.magic));
        gmk_trial_boot_request_t request;
        assert(!gmk_trial_boot_consume(&record, 8, 0, &request));
        assert(record.magic == 0);
        assert(!gmk_trial_boot_enter(&record));
    }
}

// A reset between consumption and actual entry remains a rejected trial and
// expires the internal admission. It cannot become ENTERED after reboot.
static void test_unused_admission_expires(void) {
    gmk_trial_boot_record_t record;
    prepare(&record, 1, 0);
    gmk_trial_boot_request_t request;
    assert(gmk_trial_boot_consume(&record, 8, 0, &request));
    assert(!gmk_trial_boot_consume(&record, 8, 0, &request));
    assert(gmk_trial_boot_complete(&record, 8));
    assert(!gmk_trial_boot_enter(&record));
    assert(report(&record, 4).reset_flags == 8);
    assert(!gmk_trial_boot_complete(&record, 4));
}

// Watchdog-only probes retain their action/role and show the independent reset.
// Returning trials also retain their first reset cause across normal reboots.
static void test_probe_and_return_causes(void) {
    const uint32_t reset_causes[] = {4, 8, 16, 32, 0, 0x3F, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(reset_causes) / sizeof(reset_causes[0]); ++i) {
        gmk_trial_boot_record_t record;
        prepare(&record, 2, 1);
        gmk_trial_boot_request_t request;
        assert(gmk_trial_boot_consume(&record, 40, 1, &request));
        assert(request.action == 2 && request.role == 1);
        assert(gmk_trial_boot_enter(&record));
        assert(gmk_trial_boot_complete(&record, reset_causes[i]));
        gmk_trial_boot_result_t result = report(&record, 3);
        assert(result.action == 2 && result.role == 1 && result.reset_flags == reset_causes[i]);
        gmk_trial_boot_record_t saved = record;
        assert(!gmk_trial_boot_enter(&record));
        assert(!gmk_trial_boot_consume(&record, 8, 1, &request));
        assert(!gmk_trial_boot_complete(&record, 8));
        assert(memcmp(&record, &saved, sizeof(record)) == 0);
    }
}

// Probe needs no image, and a fresh request may replace a prior completed
// result. Completing an unconsumed request only rejects it; it never enters.
static void test_empty_probe_and_fresh_request(void) {
    gmk_trial_boot_record_t record;
    assert(gmk_trial_boot_prepare(&record, 2, (gmk_image_t){0}, 1));
    gmk_trial_boot_request_t request;
    assert(gmk_trial_boot_consume(&record, 8, 1, &request));
    assert(request.action == 2 && request.image.size == 0 && request.image.crc == 0);
    assert(gmk_trial_boot_enter(&record));
    assert(gmk_trial_boot_complete(&record, 4));
    gmk_trial_boot_result_t result;
    assert(gmk_trial_boot_report(&record, &result));
    assert(result.phase == 3 && result.action == 2 && result.role == 1);
    assert(result.image.size == 0 && result.image.crc == 0 && result.reset_flags == 4);
    assert(gmk_trial_boot_prepare(&record, 1, image, 0));
    report(&record, 1);
    assert(gmk_trial_boot_complete(&record, 16));
    assert(report(&record, 4).reset_flags == 16);
    assert(!gmk_trial_boot_enter(&record));
    assert(!gmk_trial_boot_consume(&record, 8, 0, &request));
}

// Reject malformed callers without leaving an older valid launch armed. A
// missing consume output is also a consumed rejection, never a retryable RUN.
static void test_invalid_arguments_and_read_only_report(void) {
    gmk_trial_boot_record_t record;
    prepare(&record, 1, 0);
    assert(!gmk_trial_boot_enter(&record));
    assert(!gmk_trial_boot_prepare(&record, 3, image, 0));
    gmk_trial_boot_result_t result;
    assert(!gmk_trial_boot_report(&record, &result));
    prepare(&record, 1, 0);
    assert(!gmk_trial_boot_prepare(&record, 1, image, 2));
    assert(!gmk_trial_boot_report(&record, &result));
    prepare(&record, 1, 0);
    assert(!gmk_trial_boot_consume(&record, 8, 0, NULL));
    assert(!gmk_trial_boot_enter(&record));
    report(&record, 4);
    gmk_trial_boot_record_t saved = record;
    assert(gmk_trial_boot_report(&record, &result));
    assert(memcmp(&record, &saved, sizeof(record)) == 0);
    assert(!gmk_trial_boot_prepare(NULL, 1, image, 0));
    assert(!gmk_trial_boot_consume(NULL, 8, 0, NULL));
    assert(!gmk_trial_boot_enter(NULL));
    assert(!gmk_trial_boot_complete(NULL, 4));
    assert(!gmk_trial_boot_report(NULL, &result));
    assert(!gmk_trial_boot_report(&record, NULL));
}

static unsigned                       role_reads;
static bool                           role_reader_ok;
static uint32_t                       hardware_role;
static const gmk_trial_boot_record_t *record_at_role_read;

static bool read_role(uint32_t *role) {
    ++role_reads;
    if (record_at_role_read) {
        // Hardware can only be touched once the durable request is consumed.
        gmk_trial_boot_result_t result;
        assert(gmk_trial_boot_report(record_at_role_read, &result));
        assert(result.phase == 4);
    }
    *role = hardware_role;
    return role_reader_ok;
}

static void role_fixture(void) {
    role_reads          = 0;
    role_reader_ok      = true;
    hardware_role       = 0;
    record_at_role_read = NULL;
}

// Reproduces the no-intent pre-CRT GPIO regression: all ordinary boot records
// and disallowed reset causes must return before even invoking the role reader.
static void test_admit_without_intent_never_reads_hardware(void) {
    gmk_trial_boot_record_t  record;
    gmk_trial_boot_request_t request = {.action = 99};
    role_fixture();
    memset(&record, 0, sizeof(record));
    assert(!gmk_trial_boot_admit(&record, 8, read_role, &request));
    assert(role_reads == 0 && request.action == 99);
    memset(&record, 0xFF, sizeof(record));
    assert(!gmk_trial_boot_admit(&record, 8, read_role, &request));
    assert(role_reads == 0);

    gmk_trial_boot_record_t valid;
    prepare(&valid, 1, 0);
    for (unsigned byte = 0; byte < sizeof(record); ++byte) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            record = valid;
            ((uint8_t *)&record)[byte] ^= 1U << bit;
            assert(!gmk_trial_boot_admit(&record, 8, read_role, &request));
            assert(role_reads == 0);
        }
    }
    for (uint32_t flags = 0; flags < 128; ++flags) {
        if (flags == 8 || flags == 40) continue;
        record = valid;
        assert(!gmk_trial_boot_admit(&record, flags, read_role, &request));
        assert(role_reads == 0);
        assert(report(&record, 4).reset_flags == flags);
        assert(!gmk_trial_boot_enter(&record));
    }

    prepare(&record, 1, 0);
    assert(gmk_trial_boot_consume(&record, 8, 0, &request));
    assert(!gmk_trial_boot_admit(&record, 8, read_role, &request));
    assert(role_reads == 0); // Already consumed, even before ENTERED.
    assert(gmk_trial_boot_enter(&record));
    assert(!gmk_trial_boot_admit(&record, 4, read_role, &request));
    assert(role_reads == 0);
    assert(gmk_trial_boot_complete(&record, 4));
    assert(!gmk_trial_boot_admit(&record, 8, read_role, &request));
    assert(role_reads == 0);
}

// Valid requests call the role reader exactly once after consumption, including
// the right half and watchdog probes. A replay never invokes hardware again.
static void test_admit_reads_role_once_after_consumption(void) {
    for (uint32_t action = 1; action <= 2; ++action) {
        for (uint32_t role = 0; role <= 1; ++role) {
            for (unsigned pin_flag = 0; pin_flag <= 1; ++pin_flag) {
                gmk_trial_boot_record_t record;
                prepare(&record, action, role);
                gmk_trial_boot_request_t request = {.action = 99};
                role_fixture();
                hardware_role       = role;
                record_at_role_read = &record;
                assert(gmk_trial_boot_admit(&record, pin_flag ? 40 : 8, read_role, &request));
                assert(role_reads == 1);
                assert(request.action == action && request.role == role);
                assert(request.image.size == 4096 && request.image.crc == 0xA63C7195);
                assert(!gmk_trial_boot_admit(&record, 8, read_role, &request));
                assert(role_reads == 1);
                assert(gmk_trial_boot_enter(&record));
            }
        }
    }
}

// Failed or unavailable hardware checks consume the intent and revoke the
// internal admission, so later success cannot retry or directly enter it.
static void test_admit_role_failures_are_one_shot(void) {
    for (unsigned failure = 0; failure < 4; ++failure) {
        gmk_trial_boot_record_t record;
        prepare(&record, 1, 0);
        gmk_trial_boot_request_t request = {.action = 99};
        role_fixture();
        record_at_role_read = &record;
        if (failure == 1) role_reader_ok = false;
        if (failure >= 2) hardware_role = failure == 2 ? 1U : UINT32_MAX;
        assert(!gmk_trial_boot_admit(&record, 8, failure ? read_role : NULL, &request));
        assert(role_reads == (failure ? 1U : 0));
        assert(request.action == 99);
        assert(report(&record, 4).reset_flags == 8);
        assert(!gmk_trial_boot_enter(&record));
        unsigned before = role_reads;
        hardware_role   = 0;
        role_reader_ok  = true;
        assert(!gmk_trial_boot_admit(&record, 8, read_role, &request));
        assert(role_reads == before);
    }
    role_fixture();
    assert(!gmk_trial_boot_admit(NULL, 8, read_role, NULL));
    assert(role_reads == 0);
    gmk_trial_boot_record_t record;
    prepare(&record, 1, 0);
    assert(!gmk_trial_boot_admit(&record, 8, read_role, NULL));
    assert(role_reads == 0);
    assert(!gmk_trial_boot_enter(&record));
    report(&record, 4);
}

// Runtime entry may only consume an empty PROBE for the current role. It has
// no reset between request and entry, so the retained flags must remain zero.
static void test_runtime_probe_entry_and_return(void) {
    for (uint32_t role = 0; role <= 1; ++role) {
        gmk_trial_boot_record_t record;
        assert(gmk_trial_boot_prepare(&record, 2, (gmk_image_t){0}, role));
        assert(gmk_trial_boot_runtime_enter(&record, role));
        gmk_trial_boot_result_t result;
        assert(gmk_trial_boot_report(&record, &result));
        assert(result.action == 2 && result.phase == 2 && result.role == role);
        assert(result.image.size == 0 && result.image.crc == 0 && result.reset_flags == 0);
        gmk_trial_boot_record_t saved = record;
        assert(!gmk_trial_boot_runtime_enter(&record, role));
        assert(!gmk_trial_boot_enter(&record));
        assert(memcmp(&record, &saved, sizeof(record)) == 0);
        assert(gmk_trial_boot_complete(&record, 4));
        assert(gmk_trial_boot_report(&record, &result));
        assert(result.action == 2 && result.phase == 3 && result.reset_flags == 4);
        saved = record;
        assert(!gmk_trial_boot_runtime_enter(&record, role));
        assert(!gmk_trial_boot_complete(&record, 8));
        assert(memcmp(&record, &saved, sizeof(record)) == 0);
    }
}

// Runtime rejection consumes the request: RUN, nonempty probes and mismatched
// or invalid roles cannot become an executable intent on a second attempt.
static void test_runtime_rejects_run_images_and_roles(void) {
    for (unsigned failure = 0; failure < 6; ++failure) {
        gmk_trial_boot_record_t record;
        uint32_t                action    = failure == 0 ? 1U : 2U;
        gmk_image_t             bad_image = {0};
        if (failure == 1) bad_image.size = 4;
        if (failure == 2) bad_image.crc = 1;
        uint32_t role = failure >= 3 ? failure - 2 : 0;
        assert(gmk_trial_boot_prepare(&record, action, bad_image, 0));
        assert(!gmk_trial_boot_runtime_enter(&record, role));
        assert(!gmk_trial_boot_runtime_enter(&record, 0));
        assert(!gmk_trial_boot_enter(&record));
        gmk_trial_boot_request_t request;
        assert(!gmk_trial_boot_consume(&record, 8, 0, &request));
    }
}

// Bit corruption and incomplete writes cannot authorize runtime entry, and an
// admission already issued to the early launcher is never a runtime request.
static void test_runtime_corruption_and_stale_admission(void) {
    gmk_trial_boot_record_t valid;
    assert(gmk_trial_boot_prepare(&valid, 2, (gmk_image_t){0}, 0));
    for (unsigned byte = 0; byte < sizeof(valid); ++byte) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            gmk_trial_boot_record_t record = valid;
            ((uint8_t *)&record)[byte] ^= 1U << bit;
            assert(!gmk_trial_boot_runtime_enter(&record, 0));
            assert(!gmk_trial_boot_runtime_enter(&record, 0));
        }
    }
    gmk_trial_boot_request_t request;
    assert(gmk_trial_boot_consume(&valid, 8, 0, &request));
    assert(!gmk_trial_boot_runtime_enter(&valid, 0));
    assert(gmk_trial_boot_enter(&valid));
    assert(!gmk_trial_boot_runtime_enter(&valid, 0));
    assert(!gmk_trial_boot_runtime_enter(NULL, 0));
}

// These fixtures have valid CRCs over their complete retained bodies, but an
// ENTERED record with no admission reset is legal only for an empty PROBE.
// A broad zero-flags exception must never admit or report a staging RUN.
static void test_zero_reset_entry_cannot_authorize_run_or_image(void) {
    gmk_trial_boot_record_t record = {.magic = 0x474D5442, .abi = 1, .action = 1, .phase = 2, .image = {.size = 4096, .crc = 0xA63C7195}, .checksum = 0x8A836967, .checksum_inverse = 0x757C9698};
    for (unsigned i = 0; i < 2; ++i) {
        if (i) {
            record.action           = 2;
            record.checksum         = 0xCC515C03;
            record.checksum_inverse = 0x33AEA3FC;
        }
        gmk_trial_boot_result_t result;
        assert(!gmk_trial_boot_report(&record, &result));
        assert(!gmk_trial_boot_runtime_enter(&record, 0));
        assert(!gmk_trial_boot_enter(&record));
        assert(!gmk_trial_boot_complete(&record, 4));
    }
}

int main(void) {
    assert(sizeof(gmk_trial_boot_record_t) == 64);
    test_runtime_probe_entry_and_return();
    test_runtime_rejects_run_images_and_roles();
    test_runtime_corruption_and_stale_admission();
    test_zero_reset_entry_cannot_authorize_run_or_image();
    test_admit_without_intent_never_reads_hardware();
    test_admit_reads_role_once_after_consumption();
    test_admit_role_failures_are_one_shot();
    test_consumed_before_entry_and_one_shot();
    test_reset_cause_matrix();
    test_roles();
    test_corruption_and_torn_record();
    test_unused_admission_expires();
    test_probe_and_return_causes();
    test_empty_probe_and_fresh_request();
    test_invalid_arguments_and_read_only_report();
    puts("trial boot record tests passed");
    return 0;
}

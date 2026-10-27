// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/trial_platform.h"
#include "update/trial_watchdog.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static uint8_t  stage[4096], commit_page[GMK_PAGE_SIZE];
static bool     launch;
static jmp_buf  handoff;
static uint32_t flags, hardware_role;
static unsigned recoveries, feeds, writes, roles;
static bool     recovery_ok;
uint32_t        gmk_trial_platform_read(uint32_t address) {
    assert(address == 0x40010D00);
    return flags;
}
void gmk_trial_platform_write(uint32_t address, uint32_t value) {
    ++writes;
    if (launch && address != 0x40010CC8) return;
    assert(address == 0x40010CC8 && value == 1);
    flags = 0;
}
void gmk_trial_platform_barrier(void) {}
bool gmk_trial_platform_role(uint32_t *role) {
    ++roles;
    if (launch) {
        *role = hardware_role;
        return true;
    }
    assert(!"Recovery must not sample role GPIO");
    return false;
}
const uint8_t *gmk_flash_view(uint32_t address) {
    if (launch) return address == GMK_COMMIT_BASE ? commit_page : stage;
    assert(!"Recovery must not inspect flash");
    return NULL;
}
bool gmk_trial_watchdog_recover(const gmk_trial_mmio_t *io) {
    assert(io && io->read && io->write);
    ++recoveries;
    return recovery_ok;
}
bool gmk_trial_watchdog_feed(const gmk_trial_mmio_t *io) {
    assert(io);
    ++feeds;
    return true;
}
bool gmk_trial_watchdog_start(const gmk_trial_mmio_t *io) {
    if (launch) {
        gmk_trial_boot_result_t report;
        assert(gmk_trial_boot_report(&gmk_trial_record, &report));
        assert(report.phase == GMK_TRIAL_BOOT_REJECTED && !flags && !feeds && !recoveries);
        return true;
    }
    assert(!"Recovery must never start watchdog");
    return false;
}
__attribute__((noreturn)) void gmk_trial_platform_handoff(uint32_t vectors) {
    gmk_trial_boot_result_t report;
    assert(launch && vectors == GMK_STAGE_BASE);
    assert(gmk_trial_boot_report(&gmk_trial_record, &report));
    assert(report.phase == GMK_TRIAL_BOOT_ENTERED && !feeds && !recoveries);
    longjmp(handoff, 1);
}

static void entered(uint32_t role) {
    gmk_trial_boot_request_t request;
    assert(gmk_trial_boot_prepare(&gmk_trial_record, GMK_TRIAL_BOOT_RUN, (gmk_image_t){4096, 1}, role));
    assert(gmk_trial_boot_consume(&gmk_trial_record, 8, role, &request));
    assert(gmk_trial_boot_enter(&gmk_trial_record));
}
int main(void) {
    for (unsigned phase = 0; phase < 5; ++phase) {
        for (unsigned role = 0; role < 2; ++role) {
            for (unsigned current_flags = 0; current_flags < 128; ++current_flags) {
                flags = current_flags;
                memset((void *)&gmk_trial_record, 0, sizeof(gmk_trial_record));
                if (phase) entered(role);
                if (phase == 2) assert(gmk_trial_boot_complete(&gmk_trial_record, 4));
                if (phase == 3) gmk_trial_record.checksum ^= 1;
                if (phase == 4) assert(gmk_trial_boot_prepare(&gmk_trial_record, 2, (gmk_image_t){0}, role));
                recoveries = feeds = writes = roles = 0;
                recovery_ok                         = true;
                bool expected                       = phase > 0 && phase < 3 && (flags == 4 || flags == 8 || flags == 12 || flags == 36 || flags == 40 || flags == 44);
                // REQUESTED may sample GPIO on software admission, tested by
                // boot core separately; only run its unexpected-reset path.
                if (phase == 4 && (flags == 8 || flags == 40)) continue;
                gmk_trial_early_boot();
                assert(recoveries == (expected ? 1U : 0U));
                assert(!feeds && !writes && !roles);
                gmk_trial_platform_init();
                assert(recoveries == (expected ? 2U : 0U));
                gmk_trial_platform_task();
                gmk_trial_platform_task();
                assert(feeds == (expected ? 2U : 0U));
                assert(writes == 1 && !roles);
            }
        }
    }
    entered(0);
    flags       = 4;
    recovery_ok = false;
    feeds       = 0;
    gmk_trial_early_boot();
    gmk_trial_platform_init();
    gmk_trial_platform_task();
    assert(!feeds);
    assert(!gmk_trial_platform_prepare(2, (gmk_image_t){0}, 0));
    launch = true;
    memset(stage, 0, sizeof(stage));
    memset(commit_page, 0xFF, sizeof(commit_page));
    gmk_write_u32(stage, GMK_TRIAL_BOOT_RAM_END);
    for (unsigned i = 1; i <= 3; ++i)
        gmk_write_u32(stage + i * 4, GMK_STAGE_BASE + 65);
    for (hardware_role = 0; hardware_role < 2; ++hardware_role) {
        assert(gmk_trial_boot_prepare(&gmk_trial_record, 1, (gmk_image_t){sizeof(stage), gmk_crc32(stage, sizeof(stage))}, hardware_role));
        flags = 8;
        feeds = recoveries = roles = 0;
        if (!setjmp(handoff)) {
            gmk_trial_early_boot();
            assert(false);
        }
        assert(!feeds && !recoveries && roles == 1);
        assert(gmk_trial_record.role == hardware_role);
    }
    puts("trial platform recovery tests passed");
}

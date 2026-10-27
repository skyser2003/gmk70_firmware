// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "image.h"

enum {
    GMK_TRIAL_BOOT_RECORD_ADDRESS = 0x20000000,
    GMK_TRIAL_BOOT_RECORD_BYTES   = 64,
    GMK_TRIAL_BOOT_RAM_BASE       = 0x20000040,
    GMK_TRIAL_BOOT_RAM_END        = 0x20006FC0,
    GMK_TRIAL_BOOT_RUN            = 1,
    GMK_TRIAL_BOOT_PROBE          = 2,
    GMK_TRIAL_BOOT_REQUESTED      = 1,
    GMK_TRIAL_BOOT_ENTERED        = 2,
    GMK_TRIAL_BOOT_RETURNED       = 3,
    GMK_TRIAL_BOOT_REJECTED       = 4,
    GMK_TRIAL_BOOT_ROLE_LEFT      = 0,
    GMK_TRIAL_BOOT_ROLE_RIGHT     = 1,
    GMK_TRIAL_BOOT_RESET_LOWPOWER = 1,
    GMK_TRIAL_BOOT_RESET_WINDOW   = 2,
    GMK_TRIAL_BOOT_RESET_WATCHDOG = 4,
    GMK_TRIAL_BOOT_RESET_SOFTWARE = 8,
    GMK_TRIAL_BOOT_RESET_POWER    = 16,
    GMK_TRIAL_BOOT_RESET_PIN      = 32,
};

typedef struct {
    uint32_t    magic;
    uint32_t    abi;
    uint32_t    action;
    uint32_t    phase;
    gmk_image_t image;
    uint32_t    role;
    uint32_t    reset_flags;
    uint32_t    consumed; // Internal admission ticket, never another launch intent.
    uint32_t    reserved[5];
    uint32_t    checksum;
    uint32_t    checksum_inverse;
} gmk_trial_boot_record_t;

_Static_assert(sizeof(gmk_trial_boot_record_t) == GMK_TRIAL_BOOT_RECORD_BYTES, "trial retained record must be exactly 64 bytes");

typedef struct {
    uint32_t    action;
    gmk_image_t image;
    uint32_t    role;
} gmk_trial_boot_request_t;

typedef struct {
    uint32_t    action;
    uint32_t    phase;
    uint32_t    role;
    uint32_t    reset_flags;
    gmk_image_t image;
} gmk_trial_boot_result_t;

// The platform owns the volatile retained object and the reset barriers. These
// routines neither access flash/peripherals nor depend on initialized globals.
bool gmk_trial_boot_prepare(volatile gmk_trial_boot_record_t *record, uint32_t action, gmk_image_t image, uint32_t role);
// Invalidate REQUESTED before checking integrity, reset cause, or role. Accepted
// requests become REJECTED results until enter() after platform validation.
bool gmk_trial_boot_consume(volatile gmk_trial_boot_record_t *record, uint32_t reset_flags, uint32_t role, gmk_trial_boot_request_t *request);
// Consume using the integrity-checked requested role before touching hardware.
// Ordinary, corrupt, stale, and unexpected-reset boots never call read_role.
bool gmk_trial_boot_admit(volatile gmk_trial_boot_record_t *record, uint32_t reset_flags, bool (*read_role)(uint32_t *role), gmk_trial_boot_request_t *request);
bool gmk_trial_boot_enter(volatile gmk_trial_boot_record_t *record);
// Runtime has no intervening reset. Consume only an empty PROBE for this role,
// transitioning directly from REQUESTED to ENTERED with zero reset flags.
bool gmk_trial_boot_runtime_enter(volatile gmk_trial_boot_record_t *record, uint32_t role);
// On the next reset, turn ENTERED into RETURNED and expire an unused admission.
// Terminal results remain stable through later ordinary resets.
bool gmk_trial_boot_complete(volatile gmk_trial_boot_record_t *record, uint32_t reset_flags);
bool gmk_trial_boot_report(const volatile gmk_trial_boot_record_t *record, gmk_trial_boot_result_t *result);

// Upgrade an already ENTERED, empty ABI1 PROBE to diagnostic ABI2. This cannot
// authorize a launch or reset an existing diagnostic snapshot.
bool gmk_trial_boot_trace_enable(volatile gmk_trial_boot_record_t *record);
// Checkpoint each distinct access before the platform performs the MMIO. Only
// an active diagnostic probe without a captured fault can accept checkpoints.
bool gmk_trial_boot_trace_io(volatile gmk_trial_boot_record_t *record, uint32_t address, uint32_t value, bool write);
// Capture the first fault: 1 HardFault, 2 NMI, 3 MemManage, 4 BusFault, 5 UsageFault.
bool gmk_trial_boot_trace_fault(volatile gmk_trial_boot_record_t *record, uint32_t kind, uint32_t cfsr, uint32_t pc);
// Read address, value, CFSR, PC, flags (fault kind low 8 bits; write bit 8).
bool gmk_trial_boot_trace_report(const volatile gmk_trial_boot_record_t *record, uint32_t words[5]);

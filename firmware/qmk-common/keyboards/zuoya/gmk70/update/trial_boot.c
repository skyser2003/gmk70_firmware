// SPDX-License-Identifier: GPL-2.0-or-later
#include "trial_boot.h"

enum { BOOT_MAGIC = 0x474D5442, BOOT_ABI = 1, BOOT_TRACE_ABI = 2, TRACE_FAULT_MASK = 0xFF, TRACE_WRITE = 0x100 };
_Static_assert(offsetof(gmk_trial_boot_record_t, checksum) == 56, "trial record checksum covers the complete body");

static bool valid_role(uint32_t role) {
    return role == GMK_TRIAL_BOOT_ROLE_LEFT || role == GMK_TRIAL_BOOT_ROLE_RIGHT;
}

static bool software_reset(uint32_t flags) {
    return flags == GMK_TRIAL_BOOT_RESET_SOFTWARE || flags == (GMK_TRIAL_BOOT_RESET_SOFTWARE | GMK_TRIAL_BOOT_RESET_PIN);
}

static uint32_t checksum(const gmk_trial_boot_record_t *record) {
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t       crc   = UINT32_MAX;
    for (size_t i = sizeof(record->magic); i < offsetof(gmk_trial_boot_record_t, checksum); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static bool snapshot(const volatile gmk_trial_boot_record_t *record, gmk_trial_boot_record_t *copy) {
    const volatile uint8_t *source      = (const volatile uint8_t *)record;
    uint8_t                *destination = (uint8_t *)copy;
    for (size_t i = 0; i < sizeof(*copy); ++i)
        destination[i] = source[i];
    return record->magic == copy->magic;
}

static bool valid(const gmk_trial_boot_record_t *record) {
    if (record->magic != BOOT_MAGIC || (record->abi != BOOT_ABI && record->abi != BOOT_TRACE_ABI) || record->checksum_inverse != ~record->checksum || checksum(record) != record->checksum) return false;
    if ((record->action != GMK_TRIAL_BOOT_RUN && record->action != GMK_TRIAL_BOOT_PROBE) || !valid_role(record->role) || record->phase < GMK_TRIAL_BOOT_REQUESTED || record->phase > GMK_TRIAL_BOOT_REJECTED || record->consumed > 1) return false;
    if (record->abi == BOOT_TRACE_ABI) {
        // ABI2 is only an observation of an already-entered empty probe. It
        // cannot describe a launch request or carry an admission ticket.
        if (record->action != GMK_TRIAL_BOOT_PROBE || record->image.size || record->image.crc || record->consumed || (record->phase != GMK_TRIAL_BOOT_ENTERED && record->phase != GMK_TRIAL_BOOT_RETURNED)) return false;
        if ((record->reserved[4] & ~(TRACE_FAULT_MASK | TRACE_WRITE)) || (record->reserved[4] & TRACE_FAULT_MASK) > 5) return false;
    } else {
        for (unsigned i = 0; i < sizeof(record->reserved) / sizeof(record->reserved[0]); ++i)
            if (record->reserved[i] != 0) return false;
    }
    if (record->phase == GMK_TRIAL_BOOT_REQUESTED && (record->consumed || record->reset_flags)) return false;
    if (record->consumed && record->phase != GMK_TRIAL_BOOT_REJECTED) return false;
    if (record->consumed && !software_reset(record->reset_flags)) return false;
    if (record->phase == GMK_TRIAL_BOOT_ENTERED && !software_reset(record->reset_flags) && !(record->action == GMK_TRIAL_BOOT_PROBE && !record->image.size && !record->image.crc && !record->reset_flags)) return false;
    return true;
}

static void commit(volatile gmk_trial_boot_record_t *record, gmk_trial_boot_record_t *next) {
    next->checksum                = checksum(next);
    next->checksum_inverse        = ~next->checksum;
    record->magic                 = 0;
    const uint8_t    *source      = (const uint8_t *)next;
    volatile uint8_t *destination = (volatile uint8_t *)record;
    // Volatile byte copies are deliberate: neither direction may become an
    // external memcpy before the C runtime has initialized application RAM.
    for (size_t i = sizeof(record->magic); i < sizeof(*record); ++i)
        destination[i] = source[i];
    record->magic = BOOT_MAGIC; // Sole commit point, after checksum/complement.
}

bool gmk_trial_boot_prepare(volatile gmk_trial_boot_record_t *record, uint32_t action, gmk_image_t image, uint32_t role) {
    if (!record) return false;
    record->magic = 0; // Even an invalid replacement must disarm the old intent.
    if ((action != GMK_TRIAL_BOOT_RUN && action != GMK_TRIAL_BOOT_PROBE) || !valid_role(role)) return false;
    gmk_trial_boot_record_t next;
    next.magic       = 0;
    next.abi         = BOOT_ABI;
    next.action      = action;
    next.phase       = GMK_TRIAL_BOOT_REQUESTED;
    next.image       = image;
    next.role        = role;
    next.reset_flags = 0;
    next.consumed    = 0;
    for (unsigned i = 0; i < sizeof(next.reserved) / sizeof(next.reserved[0]); ++i)
        next.reserved[i] = 0;
    commit(record, &next);
    return true;
}

bool gmk_trial_boot_consume(volatile gmk_trial_boot_record_t *record, uint32_t reset_flags, uint32_t role, gmk_trial_boot_request_t *request) {
    if (!record) return false;
    gmk_trial_boot_record_t copy;
    bool                    stable = snapshot(record, &copy);
    if (copy.phase != GMK_TRIAL_BOOT_REQUESTED) return false;
    record->magic = 0; // Consume BEFORE integrity, reset-cause, or role checks.
    if (!stable || !valid(&copy) || copy.abi != BOOT_ABI) return false;
    bool accepted    = request && valid_role(role) && copy.role == role && software_reset(reset_flags);
    copy.phase       = GMK_TRIAL_BOOT_REJECTED;
    copy.reset_flags = reset_flags;
    copy.consumed    = accepted ? 1U : 0;
    commit(record, &copy);
    if (!accepted) return false;
    request->action = copy.action;
    request->image  = copy.image;
    request->role   = copy.role;
    return true;
}

bool gmk_trial_boot_admit(volatile gmk_trial_boot_record_t *record, uint32_t reset_flags, bool (*read_role)(uint32_t *role), gmk_trial_boot_request_t *request) {
    if (!record) return false;
    gmk_trial_boot_request_t candidate;
    // The stored role is only a candidate until consume validates the complete
    // record. Ordinary boots must return here without any peripheral access.
    if (!gmk_trial_boot_consume(record, reset_flags, record->role, request ? &candidate : NULL)) return false;
    uint32_t role = UINT32_MAX;
    if (!read_role || !read_role(&role) || role != candidate.role) {
        (void)gmk_trial_boot_complete(record, reset_flags); // Revoke admission.
        return false;
    }
    request->action = candidate.action;
    request->image  = candidate.image;
    request->role   = candidate.role;
    return true;
}

bool gmk_trial_boot_enter(volatile gmk_trial_boot_record_t *record) {
    if (!record) return false;
    gmk_trial_boot_record_t copy;
    if (!snapshot(record, &copy) || !valid(&copy) || copy.abi != BOOT_ABI || copy.phase != GMK_TRIAL_BOOT_REJECTED || copy.consumed != 1) return false;
    copy.phase    = GMK_TRIAL_BOOT_ENTERED;
    copy.consumed = 0;
    commit(record, &copy);
    return true;
}

bool gmk_trial_boot_runtime_enter(volatile gmk_trial_boot_record_t *record, uint32_t role) {
    if (!record) return false;
    gmk_trial_boot_record_t copy;
    bool                    stable = snapshot(record, &copy);
    if (copy.phase != GMK_TRIAL_BOOT_REQUESTED) return false;
    record->magic = 0; // Consume before checking integrity, action, or role.
    if (!stable || !valid(&copy) || copy.abi != BOOT_ABI) return false;
    bool accepted = copy.action == GMK_TRIAL_BOOT_PROBE && !copy.image.size && !copy.image.crc && valid_role(role) && copy.role == role;
    copy.phase    = accepted ? GMK_TRIAL_BOOT_ENTERED : GMK_TRIAL_BOOT_REJECTED;
    commit(record, &copy);
    return accepted;
}

bool gmk_trial_boot_complete(volatile gmk_trial_boot_record_t *record, uint32_t reset_flags) {
    if (!record) return false;
    gmk_trial_boot_record_t copy;
    if (!snapshot(record, &copy) || !valid(&copy)) return false;
    if (copy.phase == GMK_TRIAL_BOOT_ENTERED)
        copy.phase = GMK_TRIAL_BOOT_RETURNED;
    else if (copy.phase == GMK_TRIAL_BOOT_REQUESTED || (copy.phase == GMK_TRIAL_BOOT_REJECTED && copy.consumed))
        copy.phase = GMK_TRIAL_BOOT_REJECTED;
    else
        return false;
    copy.reset_flags = reset_flags;
    copy.consumed    = 0;
    commit(record, &copy);
    return true;
}

bool gmk_trial_boot_report(const volatile gmk_trial_boot_record_t *record, gmk_trial_boot_result_t *result) {
    if (!record || !result) return false;
    gmk_trial_boot_record_t copy;
    if (!snapshot(record, &copy) || !valid(&copy)) return false;
    result->action      = copy.action;
    result->phase       = copy.phase;
    result->role        = copy.role;
    result->reset_flags = copy.reset_flags;
    result->image       = copy.image;
    return true;
}

bool gmk_trial_boot_trace_enable(volatile gmk_trial_boot_record_t *record) {
    if (!record) return false;
    gmk_trial_boot_record_t copy;
    if (!snapshot(record, &copy) || !valid(&copy) || copy.abi != BOOT_ABI || copy.phase != GMK_TRIAL_BOOT_ENTERED || copy.action != GMK_TRIAL_BOOT_PROBE || copy.image.size || copy.image.crc) return false;
    copy.abi = BOOT_TRACE_ABI;
    commit(record, &copy);
    return true;
}

static bool active_trace(const volatile gmk_trial_boot_record_t *record, gmk_trial_boot_record_t *copy) {
    return record && snapshot(record, copy) && valid(copy) && copy->abi == BOOT_TRACE_ABI && copy->phase == GMK_TRIAL_BOOT_ENTERED && !(copy->reserved[4] & TRACE_FAULT_MASK);
}

bool gmk_trial_boot_trace_io(volatile gmk_trial_boot_record_t *record, uint32_t address, uint32_t value, bool write) {
    gmk_trial_boot_record_t copy;
    if (!active_trace(record, &copy)) return false;
    uint32_t flags = write ? TRACE_WRITE : 0;
    if (copy.reserved[0] == address && copy.reserved[1] == value && copy.reserved[4] == flags) return true;
    copy.reserved[0] = address;
    copy.reserved[1] = value;
    copy.reserved[4] = flags;
    commit(record, &copy);
    return true;
}

bool gmk_trial_boot_trace_fault(volatile gmk_trial_boot_record_t *record, uint32_t kind, uint32_t cfsr, uint32_t pc) {
    if (kind < 1 || kind > 5) return false;
    gmk_trial_boot_record_t copy;
    if (!active_trace(record, &copy)) return false;
    copy.reserved[2] = cfsr;
    copy.reserved[3] = pc;
    copy.reserved[4] |= kind;
    commit(record, &copy);
    return true;
}

bool gmk_trial_boot_trace_report(const volatile gmk_trial_boot_record_t *record, uint32_t words[5]) {
    if (!record || !words) return false;
    gmk_trial_boot_record_t copy;
    if (!snapshot(record, &copy) || !valid(&copy) || copy.abi != BOOT_TRACE_ABI) return false;
    for (unsigned i = 0; i < 5; ++i)
        words[i] = copy.reserved[i];
    return true;
}

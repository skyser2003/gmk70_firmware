// SPDX-License-Identifier: GPL-2.0-or-later
#include "sleep_log.h"
#include <string.h>

static uint32_t      generation, session;
static uint8_t       next_slot, latest_slot, flags;
static bool          have_latest;
static const uint8_t magic[4] = {'G', 'S', 'L', '1'};

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = n >> (8 * i);
}
uint32_t sleep_log_crc(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint32_t       crc   = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
bool sleep_log_valid(const uint8_t page[SLEEP_LOG_PAGE]) {
    return !memcmp(page, magic, 4) && get32(page + 4) && page[38] <= SLEEP_LOG_EVENTS && page[39] == 12 && !get32(page + 252) && get32(page + 248) == sleep_log_crc(page, 248);
}
static bool blank(const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; ++i)
        if (data[i] != 0xFF) return false;
    return true;
}
static bool ours(const uint8_t *page) {
    if (blank(page, SLEEP_LOG_PAGE)) return true;
    // Accept our complete marker or a prefix interrupted during programming.
    // Unknown data in the reserved region is never erased automatically.
    bool matched = false;
    for (unsigned i = 0; i < 4; ++i) {
        if (page[i] == magic[i])
            matched = true;
        else if (page[i] != 0xFF)
            return false;
    }
    return matched;
}
static bool fail(void) {
    flags |= SLEEP_LOG_IO_ERROR;
    return false;
}
void sleep_log_init(void) {
    generation = 0;
    session    = 1;
    next_slot = latest_slot = flags = 0;
    have_latest                     = false;
    if (!sleep_log_storage_init()) return;
    flags = SLEEP_LOG_READY | SLEEP_LOG_OWNED;
    uint8_t page[SLEEP_LOG_PAGE];
    for (unsigned slot = 0; slot < SLEEP_LOG_SIZE / SLEEP_LOG_PAGE; ++slot) {
        if (!sleep_log_storage_read(SLEEP_LOG_BASE + slot * SLEEP_LOG_PAGE, page, sizeof(page))) {
            fail();
            return;
        }
        if (!ours(page)) flags &= ~SLEEP_LOG_OWNED;
        uint32_t candidate = get32(page + 4);
        if (sleep_log_valid(page) && (!have_latest || (int32_t)(candidate - generation) > 0)) {
            have_latest = true;
            generation  = candidate;
            latest_slot = slot;
        }
    }
    if (have_latest) next_slot = (latest_slot + 1) % (SLEEP_LOG_SIZE / SLEEP_LOG_PAGE);
    session = generation + 1;
    if (!session) session = 1;
}
bool sleep_log_read(uint32_t offset, void *data, size_t size) {
    return (flags & SLEEP_LOG_READY) && offset < SLEEP_LOG_SIZE && size <= SLEEP_LOG_SIZE - offset && sleep_log_storage_read(SLEEP_LOG_BASE + offset, data, size);
}
uint32_t sleep_log_status(void) {
    return (sleep_log_storage_id() << 8) | flags;
}
bool sleep_log_append(uint8_t page[SLEEP_LOG_PAGE]) {
    if (flags != (SLEEP_LOG_READY | SLEEP_LOG_OWNED) || page[38] > SLEEP_LOG_EVENTS || page[39] != 12) return false;
    uint8_t check[SLEEP_LOG_PAGE];
    for (unsigned tried = 0; tried < SLEEP_LOG_SIZE / SLEEP_LOG_PAGE; ++tried) {
        uint32_t offset = next_slot * SLEEP_LOG_PAGE;
        if (!sleep_log_read(offset, check, sizeof(check))) return fail();
        if (blank(check, sizeof(check))) break;
        if (!(offset % SLEEP_LOG_SECTOR)) {
            // Never erase the sector containing the newest committed record.
            if (have_latest && latest_slot / (SLEEP_LOG_SECTOR / SLEEP_LOG_PAGE) == next_slot / (SLEEP_LOG_SECTOR / SLEEP_LOG_PAGE)) return fail();
            if (!sleep_log_storage_erase(SLEEP_LOG_BASE + offset)) return fail();
            break;
        }
        next_slot = (next_slot + 1) % (SLEEP_LOG_SIZE / SLEEP_LOG_PAGE);
        if (tried + 1 == SLEEP_LOG_SIZE / SLEEP_LOG_PAGE) return fail();
    }
    memcpy(page, magic, sizeof(magic));
    uint32_t next_generation = generation + 1;
    if (!next_generation) next_generation = 1;
    put32(page + 4, next_generation);
    put32(page + 8, session);
    put32(page + 248, sleep_log_crc(page, 248));
    memset(page + 252, 0xFF, 4);
    uint32_t address = SLEEP_LOG_BASE + next_slot * SLEEP_LOG_PAGE;
    if (!sleep_log_storage_write(address, page, SLEEP_LOG_PAGE) || !sleep_log_storage_read(address, check, sizeof(check)) || memcmp(page, check, sizeof(check))) return fail();
    uint32_t commit = 0;
    if (!sleep_log_storage_write(address + 252, &commit, sizeof(commit)) || !sleep_log_storage_read(address, check, sizeof(check)) || !sleep_log_valid(check)) return fail();
    generation  = next_generation;
    latest_slot = next_slot;
    have_latest = true;
    next_slot   = (next_slot + 1) % (SLEEP_LOG_SIZE / SLEEP_LOG_PAGE);
    return true;
}

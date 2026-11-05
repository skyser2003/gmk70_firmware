/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GMK_SETTINGS_PAGE 256U
#define GMK_SETTINGS_BANK 8192U
#define GMK_SETTINGS_BYTES (2U * GMK_SETTINGS_BANK)
#define GMK_SETTINGS_NAME_MAX 63U
#define GMK_SETTINGS_VALUE_MAX 128U
/* The 8 KiB bank's byte capacity can reject long records before this count.
 * Page versions/record bytes stay unchanged; images limited to 328 entries
 * cannot read a bank containing more than 328, so those images cannot roll
 * back a configuration using the new historical-ID headroom. */
#define GMK_SETTINGS_ENTRIES 344U /* Native bindings/controls plus historical behavior IDs. */

typedef bool (*gmk_settings_write_fn)(void *context, uint32_t offset, const uint8_t page[GMK_SETTINGS_PAGE]);
typedef struct {
    const uint8_t        *bytes;
    gmk_settings_write_fn write;
    void                 *context;
    uint64_t              generation;
    uint16_t              count;
    uint8_t               snapshot_pages, next_page;
    int8_t                bank;
    bool                  fault;
} gmk_settings_store_t;

/* Internal cursor fields; initialize before a serialized traversal. Active
 * flash pages must remain unchanged until that traversal finishes. */
typedef struct {
    uint16_t offset;
    uint8_t  journal_page;
    bool     snapshot_done;
} gmk_settings_cursor_t;

int gmk_settings_store_mount(gmk_settings_store_t *store, const uint8_t bytes[GMK_SETTINGS_BYTES], gmk_settings_write_fn write, void *context);
int gmk_settings_store_save(gmk_settings_store_t *store, const char *name, const void *value, size_t size);
/* Deliberate reset of this application's two settings banks only. Commits a
 * new empty generation in the inactive bank; interruption preserves old/new. */
int  gmk_settings_store_factory_reset(gmk_settings_store_t *store);
int  gmk_settings_store_get(const gmk_settings_store_t *store, const char *name, const uint8_t **value, size_t *size);
int  gmk_settings_store_entry(const gmk_settings_store_t *store, unsigned index, char name[GMK_SETTINGS_NAME_MAX + 1], const uint8_t **value, size_t *size);
void gmk_settings_cursor_init(gmk_settings_cursor_t *cursor);
/* Returns -ENOENT at the end. Traverses each snapshot record once, checking
 * only its bounded journal tail; no checksums are repeated after mount. */
int gmk_settings_store_next(const gmk_settings_store_t *store, gmk_settings_cursor_t *cursor, char name[GMK_SETTINGS_NAME_MAX + 1], const uint8_t **value, size_t *size);

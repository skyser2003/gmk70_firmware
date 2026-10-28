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
#define GMK_SETTINGS_ENTRIES 320U

typedef bool (*gmk_settings_write_fn)(void *context, uint32_t offset, const uint8_t page[GMK_SETTINGS_PAGE]);
typedef struct {
    uint16_t record_offset;
} gmk_settings_entry_t;

typedef struct {
    const uint8_t        *bytes;
    gmk_settings_write_fn write;
    void                 *context;
    gmk_settings_entry_t  entries[GMK_SETTINGS_ENTRIES];
    uint64_t              generation;
    uint16_t              count;
    uint8_t               next_page;
    int8_t                bank;
    bool                  fault;
} gmk_settings_store_t;

int gmk_settings_store_mount(gmk_settings_store_t *store, const uint8_t bytes[GMK_SETTINGS_BYTES], gmk_settings_write_fn write, void *context);
int gmk_settings_store_save(gmk_settings_store_t *store, const char *name, const void *value, size_t size);
int gmk_settings_store_get(const gmk_settings_store_t *store, const char *name, const uint8_t **value, size_t *size);
int gmk_settings_store_entry(const gmk_settings_store_t *store, unsigned index, char name[GMK_SETTINGS_NAME_MAX + 1], const uint8_t **value, size_t *size);

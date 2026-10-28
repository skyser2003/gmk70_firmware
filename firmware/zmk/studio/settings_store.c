/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "settings_store.h"
#include "image.h"
#include <errno.h>
#include <string.h>

/* Every write replaces one whole page. Commit is separate and written last;
 * neither an acknowledged journal page nor the current bank is rewritten. */
#define MAGIC 0x534b4d47U
#define HEADER 32U
#define PAYLOAD (GMK_SETTINGS_PAGE - HEADER)
#define PAGES (GMK_SETTINGS_BANK / GMK_SETTINGS_PAGE)
enum { SNAPSHOT = 1, JOURNAL = 2, COMMIT = 3 };

static uint16_t read16(const uint8_t *p) {
    return p[0] | (uint16_t)p[1] << 8;
}
static void write16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
static uint64_t generation(const uint8_t *p) {
    return gmk_read_u32(p + 8) | (uint64_t)gmk_read_u32(p + 12) << 32;
}

static uint32_t checksum(const uint8_t *page) {
    uint8_t copy[GMK_SETTINGS_PAGE];
    memcpy(copy, page, sizeof(copy));
    memset(copy + 28, 0, 4);
    return gmk_crc32(copy, sizeof(copy));
}

static void page_begin(uint8_t *p, unsigned kind, uint64_t gen, unsigned seq) {
    memset(p, 0xff, GMK_SETTINGS_PAGE);
    memset(p, 0, HEADER);
    gmk_write_u32(p, MAGIC);
    p[4] = 1;
    p[5] = (uint8_t)kind;
    gmk_write_u32(p + 8, (uint32_t)gen);
    gmk_write_u32(p + 12, (uint32_t)(gen >> 32));
    gmk_write_u32(p + 16, seq);
}

static bool page_valid(const uint8_t *p, unsigned kind, uint64_t gen, unsigned seq) {
    return gmk_read_u32(p) == MAGIC && p[4] == 1 && p[5] == kind && generation(p) == gen && gen != 0 && gmk_read_u32(p + 16) == seq && read16(p + 6) <= PAYLOAD && gmk_read_u32(p + 28) == checksum(p);
}

static bool erased(const uint8_t *p, size_t size) {
    for (size_t i = 0; i < size; ++i)
        if (p[i] != 0xff) return false;
    return true;
}

static int find(const gmk_settings_store_t *s, const char *name, size_t size) {
    for (unsigned i = 0; i < s->count; ++i) {
        const gmk_settings_entry_t *e = &s->entries[i];
        if (e->name_size == size && memcmp(s->bytes + e->name_offset, name, size) == 0) return (int)i;
    }
    return -1;
}

static int record_size(const uint8_t *r, unsigned available) {
    if (available < 4 || r[0] == 0 || r[0] > GMK_SETTINGS_NAME_MAX || r[1] > 1 || read16(r + 2) > GMK_SETTINGS_VALUE_MAX) return -EIO;
    unsigned size = 4U + r[0] + read16(r + 2);
    if (size > available || (r[1] && read16(r + 2))) return -EIO;
    for (unsigned i = 0; i < r[0]; ++i)
        if (r[4 + i] < 32 || r[4 + i] > 126) return -EIO;
    return (int)size;
}

static int apply(gmk_settings_store_t *s, uint32_t offset, bool snapshot) {
    const uint8_t *r     = s->bytes + offset;
    int            index = find(s, (const char *)r + 4, r[0]);
    if (snapshot && (index >= 0 || r[1])) return -EIO;
    if (r[1]) {
        if (index >= 0) {
            --s->count;
            memmove(&s->entries[index], &s->entries[index + 1], (s->count - (unsigned)index) * sizeof(*s->entries));
        }
        return 0;
    }
    if (index < 0) {
        if (s->count == GMK_SETTINGS_ENTRIES) return -EIO;
        index = s->count++;
    }
    s->entries[index] = (gmk_settings_entry_t){(uint16_t)(offset + 4), r[0], (uint8_t)read16(r + 2)};
    return 0;
}

static int scan_snapshot(gmk_settings_store_t *s, unsigned bank) {
    const uint8_t *base = s->bytes + bank * GMK_SETTINGS_BANK;
    uint64_t       gen  = generation(base);
    if (!page_valid(base, COMMIT, gen, 0) || read16(base + 6) != 12) return -EIO;
    unsigned pages = read16(base + HEADER), count = read16(base + HEADER + 2);
    if (pages >= PAGES || count > GMK_SETTINGS_ENTRIES || gmk_read_u32(base + HEADER + 4) != gmk_crc32(base + GMK_SETTINGS_PAGE, pages * GMK_SETTINGS_PAGE)) return -EIO;
    s->count = 0;
    for (unsigned i = 1; i <= pages; ++i) {
        const uint8_t *p = base + i * GMK_SETTINGS_PAGE;
        if (!page_valid(p, SNAPSHOT, gen, i) || !read16(p + 6)) return -EIO;
        unsigned end = HEADER + read16(p + 6);
        for (unsigned at = HEADER; at < end;) {
            int size = record_size(p + at, end - at);
            if (size < 0 || apply(s, bank * GMK_SETTINGS_BANK + i * GMK_SETTINGS_PAGE + at, true) != 0) return -EIO;
            at += (unsigned)size;
        }
    }
    return s->count == count ? (int)pages : -EIO;
}

int gmk_settings_store_mount(gmk_settings_store_t *s, const uint8_t bytes[GMK_SETTINGS_BYTES], gmk_settings_write_fn write, void *context) {
    if (!s || !bytes || !write) return -EINVAL;
    memset(s, 0, sizeof(*s));
    s->bytes         = bytes;
    s->write         = write;
    s->context       = context;
    s->bank          = -1;
    uint64_t gens[2] = {0, 0};
    for (unsigned b = 0; b < 2; ++b)
        if (scan_snapshot(s, b) >= 0) gens[b] = generation(bytes + b * GMK_SETTINGS_BANK);
    s->count = 0;
    if (!gens[0] && !gens[1]) {
        if (erased(bytes, GMK_SETTINGS_BYTES)) return 0;
        s->fault = true;
        return -EIO;
    }
    if (gens[0] && gens[0] == gens[1]) {
        s->fault = true;
        return -EIO;
    }
    unsigned bank  = gens[1] > gens[0];
    int      pages = scan_snapshot(s, bank);
    if (pages < 0) {
        s->fault = true;
        return -EIO;
    }
    s->bank             = (int8_t)bank;
    s->generation       = gens[bank];
    s->next_page        = (uint8_t)(pages + 1);
    const uint8_t *base = bytes + bank * GMK_SETTINGS_BANK;
    bool           tail = false;
    for (unsigned i = (unsigned)pages + 1; i < PAGES; ++i) {
        const uint8_t *p     = base + i * GMK_SETTINGS_PAGE;
        bool           valid = page_valid(p, JOURNAL, s->generation, i);
        int            size  = valid ? record_size(p + HEADER, read16(p + 6)) : -EIO;
        valid                = valid && size > 0 && (unsigned)size == read16(p + 6);
        if (!valid) {
            tail = true;
            continue;
        }
        /* A torn tail is expected; a hole before a later valid record is not. */
        if (tail || apply(s, bank * GMK_SETTINGS_BANK + i * GMK_SETTINGS_PAGE + HEADER, false) != 0) {
            s->fault = true;
            return -EIO;
        }
        s->next_page = (uint8_t)(i + 1);
    }
    return 0;
}

static int write_checked(gmk_settings_store_t *s, uint32_t offset, uint8_t *page, unsigned used) {
    if (offset % GMK_SETTINGS_PAGE || offset > GMK_SETTINGS_BYTES - GMK_SETTINGS_PAGE || used > PAYLOAD) return -EINVAL;
    write16(page + 6, (uint16_t)used);
    gmk_write_u32(page + 28, checksum(page));
    if (!s->write(s->context, offset, page) || memcmp(s->bytes + offset, page, GMK_SETTINGS_PAGE) != 0) {
        s->fault = true;
        return -EIO;
    }
    return 0;
}

static void put_record(uint8_t *p, const char *name, unsigned length, const void *value, unsigned size) {
    p[0] = (uint8_t)length;
    p[1] = value == NULL;
    write16(p + 2, (uint16_t)size);
    memcpy(p + 4, name, length);
    if (size) memcpy(p + 4 + length, value, size);
}

static unsigned snapshot_pages(const gmk_settings_store_t *s, int replacing, unsigned name_size, unsigned size, bool deleting) {
    unsigned pages = 0, used = 0;
    for (unsigned i = 0; i <= s->count; ++i) {
        if ((int)i == replacing || (i == s->count && deleting)) continue;
        unsigned n      = i == s->count ? name_size : s->entries[i].name_size;
        unsigned v      = i == s->count ? size : s->entries[i].value_size;
        unsigned record = 4 + n + v;
        if (!pages || used + record > PAYLOAD) {
            ++pages;
            used = 0;
        }
        used += record;
    }
    return pages;
}

static int compact(gmk_settings_store_t *s, const char *name, unsigned length, const void *value, unsigned size, int replacing) {
    if (s->generation == UINT64_MAX) return -EOVERFLOW;
    unsigned bank = s->bank == 0 ? 1 : 0, page_index = 1, used = 0;
    uint64_t gen = s->generation + 1;
    uint8_t  page[GMK_SETTINGS_PAGE];
    page_begin(page, SNAPSHOT, gen, page_index);
    for (unsigned i = 0; i <= s->count; ++i) {
        if ((int)i == replacing || (i == s->count && value == NULL)) continue;
        const gmk_settings_entry_t *e    = i < s->count ? &s->entries[i] : NULL;
        const char                 *key  = e ? (const char *)s->bytes + e->name_offset : name;
        const void                 *data = e ? s->bytes + e->name_offset + e->name_size : value;
        unsigned                    n = e ? e->name_size : length, v = e ? e->value_size : size;
        unsigned                    record = 4 + n + v;
        if (used + record > PAYLOAD) {
            int rc = write_checked(s, bank * GMK_SETTINGS_BANK + page_index * GMK_SETTINGS_PAGE, page, used);
            if (rc) return rc;
            ++page_index;
            used = 0;
            page_begin(page, SNAPSHOT, gen, page_index);
        }
        put_record(page + HEADER + used, key, n, data, v);
        used += record;
    }
    if (used) {
        int rc = write_checked(s, bank * GMK_SETTINGS_BANK + page_index * GMK_SETTINGS_PAGE, page, used);
        if (rc) return rc;
    } else
        --page_index;
    page_begin(page, COMMIT, gen, 0);
    write16(page + HEADER, (uint16_t)page_index);
    write16(page + HEADER + 2, (uint16_t)(s->count + (replacing < 0 && value != NULL) - (replacing >= 0 && value == NULL)));
    gmk_write_u32(page + HEADER + 4, gmk_crc32(s->bytes + bank * GMK_SETTINGS_BANK + GMK_SETTINGS_PAGE, page_index * GMK_SETTINGS_PAGE));
    memset(page + HEADER + 8, 0, 4);
    int rc = write_checked(s, bank * GMK_SETTINGS_BANK, page, 12);
    if (rc) return rc;
    return gmk_settings_store_mount(s, s->bytes, s->write, s->context);
}

int gmk_settings_store_save(gmk_settings_store_t *s, const char *name, const void *value, size_t size) {
    if (!s || !name || (!value && size)) return -EINVAL;
    size_t length = strnlen(name, GMK_SETTINGS_NAME_MAX + 1);
    if (!length) return -EINVAL;
    if (length > GMK_SETTINGS_NAME_MAX || size > GMK_SETTINGS_VALUE_MAX) return -E2BIG;
    for (unsigned i = 0; i < length; ++i)
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126) return -EINVAL;
    if (s->fault) {
        int rc = gmk_settings_store_mount(s, s->bytes, s->write, s->context);
        if (rc) return rc;
    }
    int replacing = find(s, name, length);
    if (replacing < 0 && value == NULL) return 0;
    if (replacing >= 0 && value != NULL) {
        const gmk_settings_entry_t *e = &s->entries[replacing];
        if (e->value_size == size && (!size || memcmp(s->bytes + e->name_offset + e->name_size, value, size) == 0)) return 0;
    }
    if ((replacing < 0 && s->count == GMK_SETTINGS_ENTRIES) || snapshot_pages(s, replacing, (unsigned)length, (unsigned)size, value == NULL) >= PAGES) return -ENOSPC;
    if (s->bank < 0 || s->next_page == PAGES) return compact(s, name, (unsigned)length, value, (unsigned)size, replacing);
    uint8_t page[GMK_SETTINGS_PAGE];
    page_begin(page, JOURNAL, s->generation, s->next_page);
    put_record(page + HEADER, name, (unsigned)length, value, (unsigned)size);
    uint32_t offset = (unsigned)s->bank * GMK_SETTINGS_BANK + s->next_page * GMK_SETTINGS_PAGE;
    int      rc     = write_checked(s, offset, page, (unsigned)(4 + length + size));
    if (rc) return rc;
    rc = apply(s, offset + HEADER, false);
    if (rc) {
        s->fault = true;
        return rc;
    }
    ++s->next_page;
    return 0;
}

int gmk_settings_store_get(const gmk_settings_store_t *s, const char *name, const uint8_t **value, size_t *size) {
    if (!s || !name || !value || !size) return -EINVAL;
    if (s->fault) return -EIO;
    int i = find(s, name, strlen(name));
    if (i < 0) return -ENOENT;
    const gmk_settings_entry_t *e = &s->entries[i];
    *value                        = s->bytes + e->name_offset + e->name_size;
    *size                         = e->value_size;
    return 0;
}

int gmk_settings_store_entry(const gmk_settings_store_t *s, unsigned index, char name[GMK_SETTINGS_NAME_MAX + 1], const uint8_t **value, size_t *size) {
    if (!s || !name || !value || !size) return -EINVAL;
    if (s->fault) return -EIO;
    if (index >= s->count) return -ENOENT;
    const gmk_settings_entry_t *e = &s->entries[index];
    memcpy(name, s->bytes + e->name_offset, e->name_size);
    name[e->name_size] = 0;
    *value             = s->bytes + e->name_offset + e->name_size;
    *size              = e->value_size;
    return 0;
}

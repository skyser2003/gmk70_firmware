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
    p[4] = 2;
    p[5] = (uint8_t)kind;
    gmk_write_u32(p + 8, (uint32_t)gen);
    gmk_write_u32(p + 12, (uint32_t)(gen >> 32));
    gmk_write_u32(p + 16, seq);
}

static bool page_valid(const uint8_t *p, unsigned kind, uint64_t gen, unsigned seq) {
    return gmk_read_u32(p) == MAGIC && (p[4] == 1 || p[4] == 2) && p[5] == kind && generation(p) == gen && gen != 0 && gmk_read_u32(p + 16) == seq && read16(p + 6) <= PAYLOAD && gmk_read_u32(p + 28) == checksum(p);
}

static bool erased(const uint8_t *p, size_t size) {
    for (size_t i = 0; i < size; ++i)
        if (p[i] != 0xff) return false;
    return true;
}

/* v2 canonical binding names occupy two bytes. Literal names and v1 pages
 * remain readable; compaction upgrades only the inactive bank, commit last. */
static unsigned encode_name(const char *name, unsigned length, uint8_t out[GMK_SETTINGS_NAME_MAX]) {
    if ((length == 12 || length == 13) && !memcmp(name, "keymap/l/", 9) && name[9] >= '0' && name[9] <= '3' && name[10] == '/' && name[11] >= '0' && name[11] <= '9') {
        unsigned key = (unsigned)(name[11] - '0');
        if (length == 13) {
            if (!key || name[12] < '0' || name[12] > '9') goto literal;
            key = 10 * key + (unsigned)(name[12] - '0');
        }
        if (key < 72) {
            out[0] = (uint8_t)(0x80 + name[9] - '0');
            out[1] = (uint8_t)key;
            return 2;
        }
    }
literal:
    memcpy(out, name, length);
    return length;
}

static unsigned decode_name(const uint8_t *r, char name[GMK_SETTINGS_NAME_MAX + 1]) {
    if (r[0] == 2 && r[4] >= 0x80 && r[4] <= 0x83) {
        memcpy(name, "keymap/l/0/", 11);
        name[9]      = (char)('0' + r[4] - 0x80);
        unsigned key = r[5], at = 11;
        if (key >= 10) name[at++] = (char)('0' + key / 10);
        name[at++] = (char)('0' + key % 10);
        name[at]   = 0;
        return at;
    }
    memcpy(name, r + 4, r[0]);
    name[r[0]] = 0;
    return r[0];
}

static unsigned entry_name(const uint8_t *r, uint8_t out[GMK_SETTINGS_NAME_MAX]) {
    if (r[4] >= 0x80) {
        memcpy(out, r + 4, r[0]);
        return r[0];
    }
    return encode_name((const char *)r + 4, r[0], out);
}

static bool name_matches(const uint8_t *record, const uint8_t *name, unsigned size) {
    uint8_t  encoded[GMK_SETTINGS_NAME_MAX];
    unsigned length = entry_name(record, encoded);
    return length == size && !memcmp(encoded, name, size);
}

/* All traversed records were validated at mount or read-back after writing.
 * A prefix bound also lets mount reject duplicate snapshot names before the
 * remaining snapshot pages/records have themselves been validated. */
static int find_snapshot(const gmk_settings_store_t *s, unsigned bank, unsigned pages, uint32_t before, const uint8_t *name, unsigned size) {
    uint32_t base = bank * GMK_SETTINGS_BANK;
    for (unsigned page = 1; page <= pages; ++page) {
        uint32_t start = base + page * GMK_SETTINGS_PAGE;
        if (start >= before) break;
        unsigned end = HEADER + read16(s->bytes + start + 6);
        for (unsigned at = HEADER; at < end;) {
            uint32_t offset = start + at;
            if (offset >= before) return -ENOENT;
            const uint8_t *record = s->bytes + offset;
            if (name_matches(record, name, size)) return (int)offset;
            at += 4U + record[0] + read16(record + 2);
        }
    }
    return -ENOENT;
}

static int find_journal(const gmk_settings_store_t *s, const uint8_t *name, unsigned size) {
    for (unsigned page = s->next_page; page > (unsigned)s->snapshot_pages + 1;) {
        --page;
        uint32_t offset = (unsigned)s->bank * GMK_SETTINGS_BANK + page * GMK_SETTINGS_PAGE + HEADER;
        if (name_matches(s->bytes + offset, name, size)) return (int)offset;
    }
    return -ENOENT;
}

static int find(const gmk_settings_store_t *s, const uint8_t *name, unsigned size) {
    if (s->bank < 0) return -ENOENT;
    int offset = find_journal(s, name, size);
    if (offset >= 0) return s->bytes[offset + 1] ? -ENOENT : offset;
    return find_snapshot(s, (unsigned)s->bank, s->snapshot_pages, GMK_SETTINGS_BYTES, name, size);
}

static int record_size(const uint8_t *r, unsigned available, unsigned version) {
    if (available < 4 || r[0] == 0 || r[0] > GMK_SETTINGS_NAME_MAX || r[1] > 1 || read16(r + 2) > GMK_SETTINGS_VALUE_MAX) return -EIO;
    unsigned size = 4U + r[0] + read16(r + 2);
    if (size > available || (r[1] && read16(r + 2))) return -EIO;
    if (r[0] == 2 && r[4] >= 0x80 && r[4] <= 0x83) {
        if (version != 2 || r[5] >= 72) return -EIO;
    } else {
        for (unsigned i = 0; i < r[0]; ++i)
            if (r[4 + i] < 32 || r[4 + i] > 126) return -EIO;
    }
    return (int)size;
}

static int apply_journal(gmk_settings_store_t *s, uint32_t offset) {
    const uint8_t *r = s->bytes + offset;
    uint8_t        encoded[GMK_SETTINGS_NAME_MAX];
    unsigned       n        = entry_name(r, encoded);
    int            previous = find(s, encoded, n);
    if (r[1]) {
        if (previous >= 0) --s->count;
        return 0;
    }
    if (previous < 0) {
        if (s->count == GMK_SETTINGS_ENTRIES) return -EIO;
        ++s->count;
    }
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
            int size = record_size(p + at, end - at, p[4]);
            if (size < 0 || p[at + 1]) return -EIO;
            uint8_t  encoded[GMK_SETTINGS_NAME_MAX];
            unsigned length = entry_name(p + at, encoded);
            uint32_t offset = bank * GMK_SETTINGS_BANK + i * GMK_SETTINGS_PAGE + at;
            if (find_snapshot(s, bank, pages, offset, encoded, length) >= 0 || s->count == GMK_SETTINGS_ENTRIES) return -EIO;
            ++s->count;
            at += (unsigned)size;
        }
    }
    return s->count == count ? (int)pages : -EIO;
}

/* Preserve the native role-guarded mount boundary under LTO. */
__attribute__((noipa)) int gmk_settings_store_mount(gmk_settings_store_t *s, const uint8_t bytes[GMK_SETTINGS_BYTES], gmk_settings_write_fn write, void *context) {
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
    s->snapshot_pages   = (uint8_t)pages;
    s->next_page        = (uint8_t)(pages + 1);
    const uint8_t *base = bytes + bank * GMK_SETTINGS_BANK;
    bool           tail = false;
    for (unsigned i = (unsigned)pages + 1; i < PAGES; ++i) {
        const uint8_t *p     = base + i * GMK_SETTINGS_PAGE;
        bool           valid = page_valid(p, JOURNAL, s->generation, i);
        int            size  = valid ? record_size(p + HEADER, read16(p + 6), p[4]) : -EIO;
        valid                = valid && size > 0 && (unsigned)size == read16(p + 6);
        if (!valid) {
            tail = true;
            continue;
        }
        /* A torn tail is expected; a hole before a later valid record is not. */
        if (tail || apply_journal(s, bank * GMK_SETTINGS_BANK + i * GMK_SETTINGS_PAGE + HEADER) != 0) {
            s->fault = true;
            return -EIO;
        }
        s->next_page = (uint8_t)(i + 1);
    }
    return 0;
}

void gmk_settings_cursor_init(gmk_settings_cursor_t *cursor) {
    *cursor = (gmk_settings_cursor_t){.offset = GMK_SETTINGS_PAGE + HEADER};
}

static int next_offset(const gmk_settings_store_t *s, gmk_settings_cursor_t *cursor, uint32_t *offset) {
    if (s->bank < 0) return -ENOENT;
    uint32_t base = (unsigned)s->bank * GMK_SETTINGS_BANK;
    while (!cursor->snapshot_done) {
        if (cursor->offset % GMK_SETTINGS_PAGE < HEADER) cursor->offset += (uint16_t)(HEADER - cursor->offset % GMK_SETTINGS_PAGE);
        unsigned page = cursor->offset / GMK_SETTINGS_PAGE;
        if (page > s->snapshot_pages) {
            cursor->snapshot_done = true;
            cursor->journal_page  = s->snapshot_pages + 1;
            break;
        }
        unsigned end = page * GMK_SETTINGS_PAGE + HEADER + read16(s->bytes + base + page * GMK_SETTINGS_PAGE + 6);
        if (cursor->offset >= end) {
            cursor->offset = (uint16_t)((page + 1) * GMK_SETTINGS_PAGE + HEADER);
            continue;
        }
        uint32_t       at     = base + cursor->offset;
        const uint8_t *record = s->bytes + at;
        cursor->offset += (uint16_t)(4U + record[0] + read16(record + 2));
        uint8_t  name[GMK_SETTINGS_NAME_MAX];
        unsigned length  = entry_name(record, name);
        int      journal = find_journal(s, name, length);
        if (journal >= 0) {
            if (s->bytes[journal + 1]) continue;
            at = (uint32_t)journal;
        }
        *offset = at;
        return 0;
    }
    /* At most 31 single-record journal pages remain. Emit the latest value
     * for each new name; names represented by the snapshot were emitted above. */
    while (cursor->journal_page < s->next_page) {
        unsigned       page   = cursor->journal_page++;
        uint32_t       at     = base + page * GMK_SETTINGS_PAGE + HEADER;
        const uint8_t *record = s->bytes + at;
        if (record[1]) continue;
        uint8_t  name[GMK_SETTINGS_NAME_MAX];
        unsigned length = entry_name(record, name);
        if (find_journal(s, name, length) != (int)at || find_snapshot(s, (unsigned)s->bank, s->snapshot_pages, GMK_SETTINGS_BYTES, name, length) >= 0) continue;
        *offset = at;
        return 0;
    }
    return -ENOENT;
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

static void put_record(uint8_t *p, const uint8_t *name, unsigned length, const void *value, unsigned size) {
    p[0] = (uint8_t)length;
    p[1] = value == NULL;
    write16(p + 2, (uint16_t)size);
    memcpy(p + 4, name, length);
    if (size) memcpy(p + 4 + length, value, size);
}

static unsigned snapshot_pages(const gmk_settings_store_t *s, int replacing, unsigned name_size, unsigned size, bool deleting) {
    unsigned              pages = 0, used = 0;
    gmk_settings_cursor_t cursor;
    gmk_settings_cursor_init(&cursor);
    for (unsigned i = 0; i <= s->count; ++i) {
        uint32_t offset = 0;
        if (i < s->count && next_offset(s, &cursor, &offset)) return PAGES;
        if ((int)offset == replacing || (i == s->count && deleting)) continue;
        uint8_t        encoded[GMK_SETTINGS_NAME_MAX];
        const uint8_t *r      = i == s->count ? NULL : s->bytes + offset;
        unsigned       n      = r ? entry_name(r, encoded) : name_size;
        unsigned       v      = r ? read16(r + 2) : size;
        unsigned       record = 4 + n + v;
        if (!pages || used + record > PAYLOAD) {
            ++pages;
            used = 0;
        }
        used += record;
    }
    return pages;
}

static int commit_snapshot(gmk_settings_store_t *s, unsigned bank, uint64_t gen, unsigned pages, unsigned count, uint8_t page[GMK_SETTINGS_PAGE]) {
    page_begin(page, COMMIT, gen, 0);
    write16(page + HEADER, (uint16_t)pages);
    write16(page + HEADER + 2, (uint16_t)count);
    gmk_write_u32(page + HEADER + 4, gmk_crc32(s->bytes + bank * GMK_SETTINGS_BANK + GMK_SETTINGS_PAGE, pages * GMK_SETTINGS_PAGE));
    memset(page + HEADER + 8, 0, 4);
    int result = write_checked(s, bank * GMK_SETTINGS_BANK, page, 12);
    if (result) return result;
    return gmk_settings_store_mount(s, s->bytes, s->write, s->context);
}

int gmk_settings_store_factory_reset(gmk_settings_store_t *s) {
    if (!s || !s->bytes || !s->write) return -EINVAL;
    /* Even a torn/corrupt COMMIT can leave valid future-generation journals.
     * Bound the new generation by every valid page, so an empty snapshot can
     * never revive those records. The committed headers still choose which
     * bank to preserve. Explicit reset also recovers unknown application data. */
    uint64_t gens[2] = {0, 0};
    uint64_t latest  = 0;
    for (unsigned bank = 0; bank < 2; ++bank) {
        for (unsigned index = 0; index < PAGES; ++index) {
            const uint8_t *page = s->bytes + bank * GMK_SETTINGS_BANK + index * GMK_SETTINGS_PAGE;
            uint64_t       gen  = generation(page);
            unsigned       kind = page[5];
            if (index == 0 ? kind != COMMIT || read16(page + 6) != 12 : kind != SNAPSHOT && kind != JOURNAL) continue;
            if (!page_valid(page, kind, gen, index)) continue;
            if (!index) gens[bank] = gen;
            if (gen > latest) latest = gen;
        }
    }
    if (latest == UINT64_MAX) return -EOVERFLOW;
    unsigned bank = gens[0] && gens[0] >= gens[1] ? 1U : 0U;
    uint8_t  page[GMK_SETTINGS_PAGE];
    return commit_snapshot(s, bank, latest + 1, 0, 0, page);
}

static int compact(gmk_settings_store_t *s, const uint8_t *name, unsigned length, const void *value, unsigned size, int replacing) {
    if (s->generation == UINT64_MAX) return -EOVERFLOW;
    unsigned              bank = s->bank == 0 ? 1 : 0, page_index = 1, used = 0;
    uint64_t              gen = s->generation + 1;
    uint8_t               page[GMK_SETTINGS_PAGE];
    gmk_settings_cursor_t cursor;
    gmk_settings_cursor_init(&cursor);
    page_begin(page, SNAPSHOT, gen, page_index);
    for (unsigned i = 0; i <= s->count; ++i) {
        uint32_t offset = 0;
        if (i < s->count && next_offset(s, &cursor, &offset)) return -EIO;
        if ((int)offset == replacing || (i == s->count && value == NULL)) continue;
        const uint8_t *r = i < s->count ? s->bytes + offset : NULL;
        uint8_t        encoded[GMK_SETTINGS_NAME_MAX];
        unsigned       n      = r ? entry_name(r, encoded) : length;
        const uint8_t *key    = r ? encoded : name;
        const void    *data   = r ? r + 4 + r[0] : value;
        unsigned       v      = r ? read16(r + 2) : size;
        unsigned       record = 4 + n + v;
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
    unsigned count = s->count + (unsigned)(replacing < 0 && value != NULL) - (unsigned)(replacing >= 0 && value == NULL);
    return commit_snapshot(s, bank, gen, page_index, count, page);
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
    uint8_t  encoded[GMK_SETTINGS_NAME_MAX];
    unsigned n         = encode_name(name, (unsigned)length, encoded);
    int      replacing = find(s, encoded, n);
    if (replacing < 0 && value == NULL) return 0;
    if (replacing >= 0 && value != NULL) {
        const uint8_t *r = s->bytes + (unsigned)replacing;
        if (read16(r + 2) == size && (!size || memcmp(r + 4 + r[0], value, size) == 0)) return 0;
    }
    if ((replacing < 0 && s->count == GMK_SETTINGS_ENTRIES) || snapshot_pages(s, replacing, n, (unsigned)size, value == NULL) >= PAGES) return -ENOSPC;
    if (s->bank < 0 || s->next_page == PAGES) return compact(s, encoded, n, value, (unsigned)size, replacing);
    uint8_t page[GMK_SETTINGS_PAGE];
    page_begin(page, JOURNAL, s->generation, s->next_page);
    put_record(page + HEADER, encoded, n, value, (unsigned)size);
    uint32_t offset = (unsigned)s->bank * GMK_SETTINGS_BANK + s->next_page * GMK_SETTINGS_PAGE;
    int      rc     = write_checked(s, offset, page, (unsigned)(4 + n + size));
    if (rc) return rc;
    rc = apply_journal(s, offset + HEADER);
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
    size_t length = strnlen(name, GMK_SETTINGS_NAME_MAX + 1);
    if (!length || length > GMK_SETTINGS_NAME_MAX) return -EINVAL;
    uint8_t  encoded[GMK_SETTINGS_NAME_MAX];
    unsigned n = encode_name(name, (unsigned)length, encoded);
    int      i = find(s, encoded, n);
    if (i < 0) return -ENOENT;
    const uint8_t *r = s->bytes + (unsigned)i;
    *value           = r + 4 + r[0];
    *size            = read16(r + 2);
    return 0;
}

int gmk_settings_store_entry(const gmk_settings_store_t *s, unsigned index, char name[GMK_SETTINGS_NAME_MAX + 1], const uint8_t **value, size_t *size) {
    if (!s || !name || !value || !size) return -EINVAL;
    if (s->fault) return -EIO;
    if (index >= s->count) return -ENOENT;
    gmk_settings_cursor_t cursor;
    gmk_settings_cursor_init(&cursor);
    uint32_t offset = 0;
    for (unsigned i = 0; i <= index; ++i) {
        int result = next_offset(s, &cursor, &offset);
        if (result) return result;
    }
    const uint8_t *r = s->bytes + offset;
    decode_name(r, name);
    *value = r + 4 + r[0];
    *size  = read16(r + 2);
    return 0;
}

int gmk_settings_store_next(const gmk_settings_store_t *s, gmk_settings_cursor_t *cursor, char name[GMK_SETTINGS_NAME_MAX + 1], const uint8_t **value, size_t *size) {
    if (!s || !cursor || !name || !value || !size) return -EINVAL;
    if (s->fault) return -EIO;
    uint32_t offset;
    int      result = next_offset(s, cursor, &offset);
    if (result) return result;
    const uint8_t *record = s->bytes + offset;
    decode_name(record, name);
    *value = record + 4 + record[0];
    *size  = read16(record + 2);
    return 0;
}

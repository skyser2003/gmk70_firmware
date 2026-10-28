/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "settings_store.h"
#include "image.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t  before[256], bytes[GMK_SETTINGS_BYTES], after[256];
    unsigned calls, fail_call, tear, mode;
} flash_t;

static void blank(flash_t *f) {
    memset(f, 0, sizeof(*f));
    memset(f->before, 0xa5, sizeof(f->before));
    memset(f->after, 0xa5, sizeof(f->after));
    memset(f->bytes, 0xff, sizeof(f->bytes));
}

static bool write_page(void *context, uint32_t offset, const uint8_t *page) {
    flash_t *f = context;
    assert(offset % 256 == 0 && offset <= sizeof(f->bytes) - 256);
    uint8_t *out = f->bytes + offset;
    if (++f->calls == f->fail_call) {
        if (f->mode == 0) {
            memset(out, 0xff, 256);
            memcpy(out, page, f->tear);
        } else if (f->mode == 1) {
            memset(out, 0xff, f->tear);
        } else {
            for (unsigned i = 0; i < f->tear; ++i)
                out[i] = (uint8_t)(i * 31 + 0x67);
        }
        return false;
    }
    memcpy(out, page, 256);
    return true;
}

static void mount(gmk_settings_store_t *s, flash_t *f) {
    assert(gmk_settings_store_mount(s, f->bytes, write_page, f) == 0);
}

static void expect(const gmk_settings_store_t *s, const char *name, const void *wanted, size_t length) {
    const uint8_t *value;
    size_t         size;
    int            rc = gmk_settings_store_get(s, name, &value, &size);
    if (!wanted) {
        assert(rc == -ENOENT);
        return;
    }
    assert(rc == 0 && size == length && memcmp(value, wanted, size) == 0);
}

static void basic(void) {
    flash_t              f;
    gmk_settings_store_t s;
    blank(&f);
    mount(&s, &f);
    expect(&s, "keymap/l/0/1", NULL, 0);
    uint8_t value[] = {0x12, 0, 0x34, 0, 0x56};
    assert(gmk_settings_store_save(&s, "keymap/l/0/1", value, sizeof(value)) == 0);
    mount(&s, &f);
    expect(&s, "keymap/l/0/1", value, sizeof(value));
    unsigned calls = f.calls;
    assert(gmk_settings_store_save(&s, "keymap/l/0/1", value, sizeof(value)) == 0 && f.calls == calls);
    assert(gmk_settings_store_save(&s, "missing", NULL, 0) == 0 && f.calls == calls);
    assert(gmk_settings_store_save(&s, "layer_name", "Fn", 2) == 0);
    assert(gmk_settings_store_save(&s, "keymap/l/0/1", NULL, 0) == 0);
    mount(&s, &f);
    expect(&s, "keymap/l/0/1", NULL, 0);
    expect(&s, "layer_name", "Fn", 2);
    char           name[64];
    const uint8_t *got;
    size_t         size;
    assert(gmk_settings_store_entry(&s, 0, name, &got, &size) == 0);
    assert(strcmp(name, "layer_name") == 0 && size == 2 && memcmp(got, "Fn", 2) == 0);
    assert(gmk_settings_store_entry(&s, 1, name, &got, &size) == -ENOENT);
    for (unsigned i = 0; i < 256; ++i)
        assert(f.before[i] == 0xa5 && f.after[i] == 0xa5);
}

static void bounds_and_corruption(void) {
    flash_t              f;
    gmk_settings_store_t s;
    blank(&f);
    mount(&s, &f);
    char long_name[65];
    memset(long_name, 'x', 64);
    long_name[64]      = 0;
    uint8_t value[129] = {1};
    assert(gmk_settings_store_save(&s, long_name, value, 1) == -E2BIG);
    assert(gmk_settings_store_save(&s, "key", value, sizeof(value)) == -E2BIG);
    assert(gmk_settings_store_save(&s, "", value, 1) == -EINVAL);
    assert(f.calls == 0);
    for (unsigned i = 0; i < 192; ++i) {
        char name[24];
        snprintf(name, sizeof(name), "key/%u", i);
        assert(gmk_settings_store_save(&s, name, &i, sizeof(i)) == 0);
    }
    unsigned calls = f.calls;
    assert(gmk_settings_store_save(&s, "extra", value, 1) == -ENOSPC && f.calls == calls);
    mount(&s, &f);
    for (unsigned i = 0; i < 192; ++i) {
        char name[24];
        snprintf(name, sizeof(name), "key/%u", i);
        expect(&s, name, &i, sizeof(i));
    }
    blank(&f);
    memset(f.bytes, 0x5a, 256);
    assert(gmk_settings_store_mount(&s, f.bytes, write_page, &f) == -EIO);
    assert(gmk_settings_store_save(&s, "key", value, 1) == -EIO && f.calls == 0);
}

static void long_key(char name[64], unsigned index) {
    memset(name, 'k', 60);
    snprintf(name + 60, 4, "%02u", index);
}

static void power_cuts(bool compact, bool deleting) {
    flash_t              base;
    gmk_settings_store_t s;
    blank(&base);
    mount(&s, &base);
    uint8_t old[128], newer[128];
    memset(old, 0x51, 128);
    memset(newer, 0x92, 128);
    char names[20][64];
    for (unsigned i = 0; i < 20; ++i)
        long_key(names[i], i);
    unsigned puts = compact ? 20 : 3;
    for (unsigned i = 0; i < puts; ++i)
        assert(gmk_settings_store_save(&s, names[i], old, 128) == 0);
    assert(gmk_settings_store_save(&s, names[1], NULL, 0) == 0);
    if (compact) {
        for (unsigned i = 0; i < 10; ++i) {
            old[0] = (uint8_t)i;
            assert(gmk_settings_store_save(&s, names[0], old, 128) == 0);
        }
    }
    flash_t success = base;
    mount(&s, &success);
    unsigned before = success.calls;
    assert(gmk_settings_store_save(&s, names[0], deleting ? NULL : newer, deleting ? 0 : 128) == 0);
    unsigned operations = success.calls - before;
    assert(operations == (compact ? (deleting ? 19U : 20U) : 1U));
    static const unsigned cuts[] = {0, 1, 4, 16, 32, 64, 128, 255, 256};
    for (unsigned op = 1; op <= operations; ++op)
        for (unsigned mode = 0; mode < 3; ++mode)
            for (unsigned cut = 0; cut < sizeof(cuts) / sizeof(*cuts); ++cut) {
                flash_t f = base;
                mount(&s, &f);
                f.fail_call = f.calls + op;
                f.tear      = cuts[cut];
                f.mode      = mode;
                assert(gmk_settings_store_save(&s, names[0], deleting ? NULL : newer, deleting ? 0 : 128) == -EIO);
                mount(&s, &f);
                expect(&s, names[1], NULL, 0);
                const uint8_t *got;
                size_t         size;
                int            rc = gmk_settings_store_get(&s, names[0], &got, &size);
                if (deleting && rc == -ENOENT) { /* durable new deletion */
                } else
                    assert(rc == 0 && size == 128 && (memcmp(got, old, 128) == 0 || (!deleting && memcmp(got, newer, 128) == 0)));
                uint8_t stable[128];
                memset(stable, 0x51, 128);
                for (unsigned i = 2; i < puts; ++i)
                    expect(&s, names[i], stable, 128);
                f.fail_call = 0;
                assert(gmk_settings_store_save(&s, "retry", "ok", 2) == 0);
                mount(&s, &f);
                expect(&s, "retry", "ok", 2);
            }
}

static void recalc(uint8_t *p) {
    memset(p + 28, 0, 4);
    gmk_write_u32(p + 28, gmk_crc32(p, 256));
}
static void generation_limit(void) {
    flash_t              f;
    gmk_settings_store_t s;
    blank(&f);
    mount(&s, &f);
    unsigned v = 0;
    assert(!gmk_settings_store_save(&s, "key", &v, sizeof(v)));
    while (s.next_page < 32) {
        ++v;
        assert(!gmk_settings_store_save(&s, "key", &v, sizeof(v)));
    }
    uint8_t *base = f.bytes + s.bank * GMK_SETTINGS_BANK;
    for (unsigned p = 0; p < 32; ++p) {
        memset(base + p * 256 + 8, 0xff, 8);
        recalc(base + p * 256);
    }
    gmk_write_u32(base + 36, gmk_crc32(base + 256, 256));
    recalc(base);
    mount(&s, &f);
    expect(&s, "key", &v, sizeof(v));
    unsigned before = f.calls;
    ++v;
    assert(gmk_settings_store_save(&s, "key", &v, sizeof(v)) == -EOVERFLOW && f.calls == before);
    /* Even a correctly checksummed malformed snapshot is rejected. */
    base[256 + 32] = 64;
    recalc(base + 256);
    gmk_write_u32(base + 36, gmk_crc32(base + 256, 256));
    recalc(base);
    assert(gmk_settings_store_mount(&s, f.bytes, write_page, &f) == -EIO);
}
static void format_edges(void) {
    flash_t              f;
    gmk_settings_store_t s;
    blank(&f);
    mount(&s, &f);
    f.fail_call = 1;
    f.tear      = 128;
    assert(gmk_settings_store_save(&s, "first", "x", 1) == -EIO);
    /* First provisioning has no previous bank. Unknown nonblank storage is
     * deliberately locked rather than silently reformatted after power loss. */
    assert(gmk_settings_store_mount(&s, f.bytes, write_page, &f) == -EIO);
    blank(&f);
    mount(&s, &f);
    assert(!gmk_settings_store_save(&s, "empty", "", 0));
    expect(&s, "empty", "", 0);
    /* Many bank reuses leave obsolete journal pages: generations exclude them. */
    for (unsigned i = 0; i < 500; ++i)
        assert(!gmk_settings_store_save(&s, "counter", &i, sizeof(i)));
    mount(&s, &f);
    unsigned last = 499;
    expect(&s, "counter", &last, sizeof(last));
    uint8_t *commit = f.bytes + s.bank * GMK_SETTINGS_BANK;
    /* An unsupported version is not accepted as this format. Destroy the
     * alternate bank so there is no valid fallback. */
    memset(f.bytes + (1 - s.bank) * GMK_SETTINGS_BANK, 0xff, GMK_SETTINGS_BANK);
    commit[4] = 2;
    assert(gmk_settings_store_mount(&s, f.bytes, write_page, &f) == -EIO);
}
int main(void) {
    basic();
    bounds_and_corruption();
    format_edges();
    generation_limit();
    power_cuts(false, false);
    power_cuts(false, true);
    power_cuts(true, false);
    power_cuts(true, true);
    puts("Studio settings: durable values/deletions, capacity, corruption and page-cut recovery passed");
    return 0;
}

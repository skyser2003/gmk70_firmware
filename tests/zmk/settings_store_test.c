/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "settings_store.h"
#include "image.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checksum_calls, comparison_calls;
uint32_t        __real_gmk_crc32(const void *data, size_t size);
int             __real_memcmp(const void *left, const void *right, size_t size);
uint32_t        __wrap_gmk_crc32(const void *data, size_t size) {
    ++checksum_calls;
    return __real_gmk_crc32(data, size);
}
int __wrap_memcmp(const void *left, const void *right, size_t size) {
    ++comparison_calls;
    return __real_memcmp(left, right, size);
}

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
    for (unsigned i = 0; i < GMK_SETTINGS_ENTRIES; ++i) {
        char name[24];
        snprintf(name, sizeof(name), "key/%u", i);
        assert(gmk_settings_store_save(&s, name, &i, sizeof(i)) == 0);
    }
    unsigned calls = f.calls;
    assert(gmk_settings_store_save(&s, "extra", value, 1) == -ENOSPC && f.calls == calls);
    mount(&s, &f);
    for (unsigned i = 0; i < GMK_SETTINGS_ENTRIES; ++i) {
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
    commit[4] = 3;
    assert(gmk_settings_store_mount(&s, f.bytes, write_page, &f) == -EIO);
}
static void four_layers(void) {
    flash_t              f;
    gmk_settings_store_t s;
    blank(&f);
    mount(&s, &f);
    uint8_t binding[12];
    memset(binding, 0x5b, sizeof(binding));
    for (unsigned l = 0; l < 4; ++l)
        for (unsigned k = 0; k < 72; ++k) {
            char name[32];
            snprintf(name, sizeof(name), "keymap/l/%u/%u", l, k);
            assert(gmk_settings_store_save(&s, name, binding, sizeof(binding)) == 0);
        }
    for (unsigned i = 0; i < 16; ++i) {
        char name[32], device[32];
        snprintf(name, sizeof(name), "behavior/local_id/%u", i);
        memset(device, 'b', sizeof(device));
        assert(!gmk_settings_store_save(&s, name, device, sizeof(device)));
    }
    for (unsigned l = 0; l < 4; ++l) {
        char name[32];
        snprintf(name, sizeof(name), "keymap/l_n/%u", l);
        assert(!gmk_settings_store_save(&s, name, "ABCDEFGHIJKLMNOPQRS", 19));
    }
    uint8_t order[] = {0, 1, 2, 3}, rgb[16] = {1};
    assert(!gmk_settings_store_save(&s, "keymap/layer_order", order, sizeof(order)));
    assert(!gmk_settings_store_save(&s, "rgb/underglow/state", rgb, sizeof(rgb)));
    mount(&s, &f);
    assert(s.count == 310);
    /* Append encoder positions without renumbering/reformatting old bindings.
     * These names deliberately use the legacy-compatible literal encoding. */
    for (unsigned l = 0; l < 4; ++l)
        for (unsigned k = 72; k < 74; ++k) {
            char name[32];
            snprintf(name, sizeof(name), "keymap/l/%u/%u", l, k);
            assert(!gmk_settings_store_save(&s, name, binding, sizeof(binding)));
            mount(&s, &f);
            expect(&s, name, binding, sizeof(binding));
        }
    uint8_t enabled = 1;
    assert(!gmk_settings_store_save(&s, "gmk70/nkro", &enabled, 1));
    assert(!gmk_settings_store_save(&s, "gmk70/function_flip", &enabled, 1));
    mount(&s, &f);
    assert(s.count == 320);
    for (unsigned l = 0; l < 4; ++l)
        for (unsigned k = 0; k < 72; ++k) {
            char name[32];
            snprintf(name, sizeof(name), "keymap/l/%u/%u", l, k);
            expect(&s, name, binding, sizeof(binding));
        }
    /* Exercise several subsequent full compactions at the capacity boundary. */
    for (unsigned i = 0; i < 90; ++i) {
        binding[0] = (uint8_t)i;
        assert(!gmk_settings_store_save(&s, "keymap/l/3/71", binding, sizeof(binding)));
        mount(&s, &f);
        expect(&s, "keymap/l/3/71", binding, sizeof(binding));
    }
}
/* Construct the installed v1 representation independently from the writer. */
static void legacy_page(uint8_t *p, unsigned kind, unsigned sequence, const uint8_t value[12]) {
    memset(p, 0xff, 256);
    memset(p, 0, 32);
    gmk_write_u32(p, 0x534b4d47);
    p[4] = 1;
    p[5] = (uint8_t)kind;
    gmk_write_u32(p + 8, 1);
    gmk_write_u32(p + 16, sequence);
    p[6]  = 29;
    p[32] = 13;
    p[33] = 0;
    p[34] = 12;
    p[35] = 0;
    memcpy(p + 36, "keymap/l/1/71", 13);
    memcpy(p + 49, value, 12);
    recalc(p);
}
static void native_capacity_with_id_history(void) {
    static const char *const behaviors[21] = {"mouse_key_press", "mouse_move", "mouse_scroll", "sysreset", "extpower", "gmk_battery", "gmk_controls", "gmk_flip", "gmk_radio", "gmk_sleep", "gmk_recovery", "studio_unlock", "bootload", "z_so_off", "gmk_nkro", "key_press", "momentary_layer", "to_layer", "transparent", "none", "rgb_ug"};
    flash_t                  flash;
    gmk_settings_store_t     store;
    blank(&flash);
    mount(&store, &flash);
    for (unsigned i = 0; i < 21; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "behavior/local_id/%u", i + 1);
        assert(!gmk_settings_store_save(&store, name, behaviors[i], strlen(behaviors[i])));
    }
    /* Native packed binding: uint16 local ID + two little-endian uint32 params.
     * Keep the legal full ten-byte representation, including a zero param2. */
    uint8_t binding[10] = {16};
    for (unsigned layer = 0; layer < 4; ++layer)
        for (unsigned position = 0; position < 74; ++position) {
            char name[32];
            snprintf(name, sizeof(name), "keymap/l/%u/%u", layer, position);
            gmk_write_u32(binding + 2, 0x00070004U + position % 26);
            assert(!gmk_settings_store_save(&store, name, binding, sizeof(binding)));
        }
    for (unsigned layer = 0; layer < 4; ++layer) {
        char name[32];
        snprintf(name, sizeof(name), "keymap/l_n/%u", layer);
        assert(!gmk_settings_store_save(&store, name, "ABCDEFGHIJKLMNOPQRS", 19));
    }
    uint8_t order[4] = {0, 1, 2, 3}, rgb[10] = {130, 0, 80, 30, 1, 0, 0, 0, 1, 0};
    uint8_t enabled = 1, controls[4] = {1, 0, 5, 7}, selected = 0;
    assert(!gmk_settings_store_save(&store, "keymap/layer_order", order, sizeof(order)));
    assert(!gmk_settings_store_save(&store, "rgb/underglow/state", rgb, sizeof(rgb)));
    assert(!gmk_settings_store_save(&store, "gmk_flip/state", &enabled, 1));
    assert(!gmk_settings_store_save(&store, "gmk_nkro/state", &enabled, 1));
    assert(!gmk_settings_store_save(&store, "gmk_controls/state", controls, sizeof(controls)));
    assert(!gmk_settings_store_save(&store, "physical_layouts/selected", &selected, 1));
    assert(!gmk_settings_store_save(&store, "endpoints/preferred2", &enabled, 1));
    assert(store.count == 328);
    for (unsigned i = 0; i < 16; ++i) {
        char name[32], device[32];
        snprintf(name, sizeof(name), "behavior/local_id/%u", 100 + i);
        snprintf(device, sizeof(device), "old_behavior_%02u", i);
        assert(!gmk_settings_store_save(&store, name, device, strlen(device)));
    }
    assert(store.count == 344);
    for (unsigned i = 0; i <= GMK_SETTINGS_BANK / GMK_SETTINGS_PAGE; ++i) {
        rgb[0] = (uint8_t)i;
        assert(!gmk_settings_store_save(&store, "rgb/underglow/state", rgb, sizeof(rgb)));
    }
    mount(&store, &flash);
    assert(store.count == 344 && store.snapshot_pages < GMK_SETTINGS_BANK / GMK_SETTINGS_PAGE);
    expect(&store, "gmk_controls/state", controls, sizeof(controls));
    expect(&store, "behavior/local_id/115", "old_behavior_15", 15);
    printf("Native capacity: 296 bindings,21 IDs,4 names,7 preferences,16 historical IDs fit %u snapshot pages plus commit\n", store.snapshot_pages);
}
static void legacy_migration(void) {
    flash_t              f;
    gmk_settings_store_t s;
    blank(&f);
    uint8_t old[12], newer[12];
    memset(old, 0x34, sizeof(old));
    memset(newer, 0x59, sizeof(newer));
    for (unsigned i = 1; i < 32; ++i)
        legacy_page(f.bytes + i * 256, i == 1 ? 1 : 2, i, old);
    uint8_t *commit = f.bytes;
    legacy_page(commit, 3, 0, old);
    commit[6] = 12;
    memset(commit + 32, 0, 224);
    commit[32] = commit[34] = 1;
    gmk_write_u32(commit + 36, gmk_crc32(commit + 256, 256));
    recalc(commit);
    mount(&s, &f);
    assert(f.calls == 0 && s.count == 1 && s.next_page == 32);
    expect(&s, "keymap/l/1/71", old, sizeof(old));
    for (unsigned op = 1; op <= 2; ++op)
        for (unsigned cut = 0; cut <= 256; ++cut) {
            flash_t trial = f;
            mount(&s, &trial);
            trial.fail_call = op;
            trial.tear      = cut;
            assert(gmk_settings_store_save(&s, "keymap/l/1/71", newer, sizeof(newer)) == -EIO);
            mount(&s, &trial);
            const uint8_t *got;
            size_t         size;
            assert(!gmk_settings_store_get(&s, "keymap/l/1/71", &got, &size));
            assert(size == 12 && (!memcmp(got, old, 12) || !memcmp(got, newer, 12)));
        }
    mount(&s, &f);
    assert(!gmk_settings_store_save(&s, "keymap/l/1/71", newer, sizeof(newer)));
    assert(f.bytes[s.bank * GMK_SETTINGS_BANK + 4] == 2);
    mount(&s, &f);
    expect(&s, "keymap/l/1/71", newer, sizeof(newer));
    char           name[64];
    const uint8_t *got;
    size_t         size;
    assert(!gmk_settings_store_entry(&s, 0, name, &got, &size));
    assert(!strcmp(name, "keymap/l/1/71"));
}
static void factory_reset_transaction(void) {
    flash_t              base;
    gmk_settings_store_t store;
    blank(&base);
    mount(&store, &base);
    assert(!gmk_settings_store_save(&store, "keymap/test", "layout", 6));
    assert(!gmk_settings_store_save(&store, "gmk_controls/state", "prefs", 5));
    for (unsigned n = 0; n < 40; ++n)
        assert(!gmk_settings_store_save(&store, "counter", &n, sizeof(n)));
    unsigned active = (unsigned)store.bank;
    /* A failed/corrupt newer COMMIT can leave valid future-generation journal
     * pages in the target bank. Reset must never revive those stale records. */
    flash_t damaged = base;
    damaged.bytes[active * GMK_SETTINGS_BANK] ^= 1;
    mount(&store, &damaged);
    assert(!gmk_settings_store_factory_reset(&store));
    mount(&store, &damaged);
    assert(store.count == 0);
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned cut = 0; cut <= GMK_SETTINGS_PAGE; ++cut) {
            flash_t trial = base;
            mount(&store, &trial);
            trial.fail_call = trial.calls + 1;
            trial.tear      = cut;
            trial.mode      = mode;
            assert(gmk_settings_store_factory_reset(&store) == -EIO);
            assert(!memcmp(trial.bytes + active * GMK_SETTINGS_BANK, base.bytes + active * GMK_SETTINGS_BANK, GMK_SETTINGS_BANK));
            mount(&store, &trial);
            assert(store.count == 0 || store.count == 3);
            if (store.count) {
                expect(&store, "keymap/test", "layout", 6);
                expect(&store, "gmk_controls/state", "prefs", 5);
                unsigned last = 39;
                expect(&store, "counter", &last, sizeof(last));
            }
        }
    mount(&store, &base);
    unsigned writes = base.calls;
    assert(!gmk_settings_store_factory_reset(&store));
    assert(base.calls == writes + 1 && store.count == 0);
    mount(&store, &base);
    expect(&store, "keymap/test", NULL, 0);
    expect(&store, "gmk_controls/state", NULL, 0);
    assert(!gmk_settings_store_save(&store, "new", "ok", 2));
    mount(&store, &base);
    assert(store.count == 1);
    expect(&store, "new", "ok", 2);
    assert(!gmk_settings_store_factory_reset(&store));
    mount(&store, &base);
    assert(store.count == 0);
    for (unsigned i = 0; i < sizeof(base.before); ++i)
        assert(base.before[i] == 0xa5 && base.after[i] == 0xa5);
    /* Deliberate recovery can replace a corrupt application-owned bank. */
    memset(base.bytes, 0x5a, sizeof(base.bytes));
    assert(gmk_settings_store_mount(&store, base.bytes, write_page, &base) == -EIO);
    assert(!gmk_settings_store_factory_reset(&store));
    mount(&store, &base);
    assert(store.count == 0);
    uint8_t *commit = base.bytes + (unsigned)store.bank * GMK_SETTINGS_BANK;
    gmk_write_u32(commit + 8, UINT32_MAX);
    gmk_write_u32(commit + 12, UINT32_MAX);
    recalc(commit);
    writes = base.calls;
    assert(gmk_settings_store_factory_reset(&store) == -EOVERFLOW && base.calls == writes);
}

static void streaming_capacity_and_read_bounds(void) {
    enum { SAVED_ENTRIES = 328 };
    flash_t              flash;
    gmk_settings_store_t store;
    blank(&flash);
    mount(&store, &flash);
    uint32_t expected[SAVED_ENTRIES + 1];
    for (unsigned i = 0; i < SAVED_ENTRIES; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "key/%u", i);
        expected[i] = i;
        assert(!gmk_settings_store_save(&store, name, &expected[i], sizeof(expected[i])));
    }
    assert(store.count == SAVED_ENTRIES);
    assert(!gmk_settings_store_save(&store, "key/5", NULL, 0));
    expected[SAVED_ENTRIES] = 400;
    assert(!gmk_settings_store_save(&store, "new", &expected[SAVED_ENTRIES], sizeof(expected[SAVED_ENTRIES])));
    assert(!gmk_settings_store_save(&store, "key/10", NULL, 0));
    expected[10] = 1000;
    assert(!gmk_settings_store_save(&store, "key/10", &expected[10], sizeof(expected[10])));
    for (unsigned i = 0; i < 64; ++i) {
        expected[2] = 2000 + i;
        assert(!gmk_settings_store_save(&store, "key/2", &expected[2], sizeof(expected[2])));
    }
    checksum_calls = 0;
    mount(&store, &flash);
    assert(store.count == SAVED_ENTRIES && checksum_calls <= 4 * (GMK_SETTINGS_BANK / GMK_SETTINGS_PAGE + 2));
    checksum_calls = comparison_calls = 0;
    const uint8_t *value;
    size_t         size;
    assert(gmk_settings_store_get(&store, "key/999", &value, &size) == -ENOENT);
    unsigned lookup_comparisons = comparison_calls;
    assert(lookup_comparisons <= GMK_SETTINGS_ENTRIES + GMK_SETTINGS_BANK / GMK_SETTINGS_PAGE);
    comparison_calls = 0;
    gmk_settings_cursor_t cursor;
    gmk_settings_cursor_init(&cursor);
    bool     seen[SAVED_ENTRIES + 1] = {false};
    unsigned count                   = 0;
    char     name[GMK_SETTINGS_NAME_MAX + 1];
    while (!gmk_settings_store_next(&store, &cursor, name, &value, &size)) {
        unsigned index = SAVED_ENTRIES;
        if (strcmp(name, "new")) {
            assert(!strncmp(name, "key/", 4));
            char         *end;
            unsigned long parsed = strtoul(name + 4, &end, 10);
            assert(!*end && parsed < SAVED_ENTRIES);
            index = (unsigned)parsed;
        }
        assert(!seen[index] && size == sizeof(uint32_t));
        seen[index] = true;
        uint32_t actual;
        memcpy(&actual, value, size);
        assert(actual == expected[index]);
        ++count;
    }
    unsigned iteration_comparisons = comparison_calls;
    assert(count == SAVED_ENTRIES && !seen[5] && seen[SAVED_ENTRIES]);
    assert(checksum_calls == 0);
    assert(iteration_comparisons <= 2 * (GMK_SETTINGS_ENTRIES + GMK_SETTINGS_BANK / GMK_SETTINGS_PAGE) * (GMK_SETTINGS_BANK / GMK_SETTINGS_PAGE));
    assert(gmk_settings_store_next(&store, &cursor, name, &value, &size) == -ENOENT);
    printf("Streaming settings: store=%zu cursor=%zu bytes, missing lookup=%u and full%u iteration=%u comparisons, zero lookup CRCs\n", sizeof(store), sizeof(cursor), lookup_comparisons, SAVED_ENTRIES, iteration_comparisons);
}

int main(void) {
    /* The retained application RAM admits a streaming store, not an index
     * containing one RAM offset for every possible saved binding. */
    assert(sizeof(gmk_settings_store_t) <= 40);
    native_capacity_with_id_history();
    streaming_capacity_and_read_bounds();
    factory_reset_transaction();
    four_layers();
    legacy_migration();
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

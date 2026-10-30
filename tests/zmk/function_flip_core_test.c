/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../../firmware/zmk/function_flip/core.h"

static const uint32_t mac_number[12]    = {0xc0070, 0xc006f, 0x0807002b, 0x08070008, 0, 0, 0xc00b6, 0xc00cd, 0xc00b5, 0xc00e2, 0xc00ea, 0xc00e9};
static const uint32_t mac_function[12]  = {0xc0183, 0xc00ea, 0xc00e9, 0xc00e2, 0xc00b7, 0xc00b6, 0xc00cd, 0xc00b5, 0xc018a, 0xc0223, 0xc0192, 0xc0221};
static const uint8_t  number_usages[12] = {0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e};
static void           complete_stock_tables(void) {
    struct gmk_flip_core core = {0};
    for (unsigned enabled = 0; enabled < 2; ++enabled)
        for (unsigned mac = 0; mac < 2; ++mac)
            for (unsigned family = 0; family < 2; ++family)
                for (unsigned i = 0; i < 12; ++i) {
                    core.enabled                    = enabled;
                    uint32_t               input    = 0x70000 | (family ? 0x3a + i : number_usages[i]);
                    uint32_t               expected = !enabled ? input : mac ? (family ? mac_function[i] : mac_number[i]) : family ? input : 0x7003a + i;
                    struct gmk_flip_output down     = gmk_flip_core_event(&core, input, true, mac);
                    struct gmk_flip_output up       = gmk_flip_core_event(&core, input, false, mac);
                    assert(!down.release_previous && !up.release_previous);
                    assert(down.emit == (expected != 0) && up.emit == (expected != 0));
                    if (expected) assert(down.key == expected && up.key == expected);
                    assert(down.brightness == (enabled && mac && !family && i == 4 ? -1 : enabled && mac && !family && i == 5 ? 1 : 0));
                    assert(up.brightness == 0);
                }
}
static void hold_context_and_modifiers(void) {
    struct gmk_flip_core   core = {0};
    struct gmk_flip_output out  = gmk_flip_core_event(&core, 0x0207001e, true, false);
    assert(out.key == 0x0207001e);
    core.enabled = true;
    out          = gmk_flip_core_event(&core, 0x7001e, false, true);
    assert(out.key == 0x0207001e);
    out = gmk_flip_core_event(&core, 0x02070020, true, true);
    assert(out.key == 0x0a07002b);
    core.enabled = false;
    out          = gmk_flip_core_event(&core, 0x70020, false, false);
    assert(out.key == 0x0a07002b);
    core.enabled = true;
    out          = gmk_flip_core_event(&core, 0x70022, true, true);
    assert(!out.emit && out.brightness == -1);
    core.enabled = false;
    out          = gmk_flip_core_event(&core, 0x70022, false, false);
    assert(!out.emit && !out.brightness);
}
static void duplicate_source_releases_old_target_and_unrelated_keys_pass(void) {
    struct gmk_flip_core   core = {.enabled = true};
    struct gmk_flip_output out  = gmk_flip_core_event(&core, 0x7001e, true, false);
    assert(out.key == 0x7003a && !out.release_previous);
    out = gmk_flip_core_event(&core, 0x0107001e, true, true);
    assert(out.release_previous && out.previous == 0x7003a && out.key == 0x010c0070);
    out = gmk_flip_core_event(&core, 0x7001e, false, false);
    assert(out.key == 0x010c0070);
    const uint32_t ordinary[] = {0x70004, 0xc00ea, 0x700e1, 0x70073, 0x10081, 0x70028, 0x70029, 0x7002a, 0x7002b, 0x7002c};
    for (unsigned i = 0; i < sizeof(ordinary) / sizeof(ordinary[0]); ++i) {
        out = gmk_flip_core_event(&core, ordinary[i], true, true);
        assert(out.emit && out.key == ordinary[i] && !out.release_previous && !out.brightness);
    }
    out = gmk_flip_core_event(&core, 0x7001f, false, true);
    assert(out.emit && out.key == 0x7001f); /* No matching press: no invented target. */
}
int main(void) {
    complete_stock_tables();
    hold_context_and_modifiers();
    duplicate_source_releases_old_target_and_unrelated_keys_pass();
    puts("Function flip core: all stock mappings, held context, modifiers, duplicates and brightness passed");
}

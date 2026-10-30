/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.h"

/* Stock KC_FILP tables: qmk-common/gmk70.c, number and F-row handlers.
 * Encoded values use native ZMK HID page/id plus implicit modifier byte.
 * Zero entries are keyboard-light brightness actions, not host keycodes. */
static const uint32_t mac_numbers[12]   = {0xc0070, 0xc006f, 0x0807002b, 0x08070008, 0, 0, 0xc00b6, 0xc00cd, 0xc00b5, 0xc00e2, 0xc00ea, 0xc00e9};
static const uint32_t mac_functions[12] = {0xc0183, 0xc00ea, 0xc00e9, 0xc00e2, 0xc00b7, 0xc00b6, 0xc00cd, 0xc00b5, 0xc018a, 0xc0223, 0xc0192, 0xc0221};

static uint32_t mapped(unsigned index, unsigned context, uint8_t mods) {
    uint32_t key = 0x70000 | (index < 10 ? 0x1e + index : index < 12 ? 0x2d + index - 10 : 0x3a + index - 12);
    if (context == 2 && index < 12) key = 0x7003a + index;
    if (context == 3) key = index < 12 ? mac_numbers[index] : mac_functions[index - 12];
    return key ? key | ((uint32_t)mods << 24) : 0;
}
struct gmk_flip_output gmk_flip_core_event(struct gmk_flip_core *core, uint32_t encoded, bool pressed, bool mac) {
    struct gmk_flip_output out = {.key = encoded, .emit = true};
    uint32_t               key = encoded & 0xffffffU;
    unsigned               index;
    if (key >= 0x7001e && key <= 0x70027)
        index = key - 0x7001e;
    else if (key == 0x7002d || key == 0x7002e)
        index = key - 0x7002d + 10;
    else if (key >= 0x7003a && key <= 0x70045)
        index = key - 0x7003a + 12;
    else
        return out;
    unsigned context = core->held[index] >> 8;
    uint8_t  mods    = core->held[index];
    if (pressed) {
        if (context) {
            out.previous         = mapped(index, context, mods);
            out.release_previous = out.previous != 0;
        }
        context           = !core->enabled ? 1 : mac ? 3 : 2;
        mods              = encoded >> 24;
        core->held[index] = (uint16_t)((context << 8) | mods);
    } else {
        core->held[index] = 0;
        if (!context) return out;
    }
    out.key  = mapped(index, context, mods);
    out.emit = out.key != 0;
    if (pressed && context == 3 && (index == 4 || index == 5)) out.brightness = index == 4 ? -1 : 1;
    return out;
}

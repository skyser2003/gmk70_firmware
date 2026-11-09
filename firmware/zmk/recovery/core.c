/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "recovery.h"
#include <errno.h>
#include <dt-bindings/zmk/reset.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/sys_io.h>
#include <zmk/settings.h>

/* Pinned QMK wb32_dfu.c writes __ram0_end__ - 4. The unchanged resident's
 * update/boot.c (or the standalone SoC reset hook) consumes this exact word
 * before entering WB32 ROM.
 * Zephyr's admitted RAM ends at 0x20006fc0; this is the separately reserved
 * factory-magic word, not the end of Zephyr's smaller application RAM region. */
#define FACTORY_BOOT_MAGIC_ADDRESS 0x20006ffcU
#define FACTORY_BOOT_MAGIC 0xdeadbeefU
#define STARTUP_CHORD_WINDOW_MS 250U
#define RECOVERY_HOLD_MS 3000U

_Noreturn void __real_sys_reboot(int type);
_Noreturn void __wrap_sys_reboot(int type) {
    /* Native &bootloader uses RST_UF2 without retained bootmode support. Map
     * that native intent to the existing factory protocol, then let Zephyr
     * own interrupt shutdown and the architecture's reset barriers/sequence. */
    sys_write32(type == RST_UF2 ? FACTORY_BOOT_MAGIC : 0U, FACTORY_BOOT_MAGIC_ADDRESS);
    __real_sys_reboot(type == RST_UF2 ? SYS_REBOOT_WARM : type);
}

int gmk_recovery_request(enum gmk_recovery_action action) {
    if (k_is_in_isr()) return -EWOULDBLOCK;
    switch (action) {
        case GMK_RECOVERY_RESET:
            sys_reboot(SYS_REBOOT_WARM);
        case GMK_RECOVERY_BOOTLOADER:
            sys_reboot(RST_UF2);
        case GMK_RECOVERY_SETTINGS_RESET: {
            /* Root's native API wrapper owns authorization, serialization and
             * application-bank exclusion. Never fall back to a raw flash wipe. */
            int result = zmk_settings_erase();
            if (result) return result;
            sys_reboot(SYS_REBOOT_WARM);
        }
        default:
            return -EINVAL;
    }
}

enum gmk_recovery_action gmk_recovery_startup_sample(struct gmk_recovery_startup *state, bool bootloader_chord, bool settings_chord, uint32_t now_ms) {
    if (!state || state->finished) return GMK_RECOVERY_NONE;
    enum gmk_recovery_action action = settings_chord ? GMK_RECOVERY_SETTINGS_RESET : bootloader_chord ? GMK_RECOVERY_BOOTLOADER : GMK_RECOVERY_NONE;
    if (!state->sampled) {
        state->sampled    = true;
        state->started_at = now_ms;
        state->candidate  = action;
    }
    if (state->candidate == GMK_RECOVERY_NONE) {
        if ((uint32_t)(now_ms - state->started_at) > STARTUP_CHORD_WINDOW_MS) {
            state->finished = true;
            return GMK_RECOVERY_NONE;
        }
        if (action != GMK_RECOVERY_NONE) {
            state->candidate  = action;
            state->started_at = now_ms;
        }
        return GMK_RECOVERY_NONE;
    }
    if (action != state->candidate) {
        state->finished = true;
        return GMK_RECOVERY_NONE;
    }
    if ((uint32_t)(now_ms - state->started_at) < RECOVERY_HOLD_MS) return GMK_RECOVERY_NONE;
    state->finished = true;
    return action;
}

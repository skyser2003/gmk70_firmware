/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../firmware/zmk/recovery/recovery.h"
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <dt-bindings/zmk/reset.h>
#include <zephyr/sys/reboot.h>

static uint8_t  lower_retained[64], upper_retained[64];
static jmp_buf  reset_jump;
static int      reset_type, erase_result;
static unsigned resets, erases, magic_writes;
static bool     in_isr;
int             native_reset_press(int type);

bool k_is_in_isr(void) {
    return in_isr;
}
void sys_write32(uint32_t value, uintptr_t address) {
    assert(address == 0x20006ffcU);
    memcpy(upper_retained + 60, &value, sizeof(value));
    ++magic_writes;
}
_Noreturn void sys_reboot(int type) {
    reset_type = type;
    ++resets;
    longjmp(reset_jump, 1);
}
int zmk_settings_erase(void) {
    ++erases;
    return erase_result;
}

static uint32_t magic(void) {
    uint32_t value;
    memcpy(&value, upper_retained + 60, sizeof(value));
    return value;
}
static void start(void) {
    memset(lower_retained, 0xa5, sizeof(lower_retained));
    memset(upper_retained, 0x5a, sizeof(upper_retained));
    reset_type = erase_result = 0;
    resets = erases = magic_writes = 0;
    in_isr                         = false;
}
static void check_retained(void) {
    for (unsigned i = 0; i < sizeof(lower_retained); ++i)
        assert(lower_retained[i] == 0xa5);
    for (unsigned i = 0; i < 60; ++i)
        assert(upper_retained[i] == 0x5a);
}

/* Mapping RST_UF2 to an invented reset type instead of factory magic fails. */
static void test_pinned_native_bootloader_behavior(void) {
    start();
    if (!setjmp(reset_jump)) {
        (void)native_reset_press(RST_UF2);
        assert(false);
    }
    assert(resets == 1 && reset_type == SYS_REBOOT_WARM);
    assert(magic_writes == 1 && magic() == 0xdeadbeefU);
    assert(erases == 0);
    check_retained();
}

/* Normal reboot must clear only the magic word and preserve native reset type. */
static void test_pinned_native_normal_reset_behavior(void) {
    start();
    if (!setjmp(reset_jump)) {
        (void)native_reset_press(RST_COLD);
        assert(false);
    }
    assert(resets == 1 && reset_type == SYS_REBOOT_COLD);
    assert(magic_writes == 1 && magic() == 0);
    assert(erases == 0);
    check_retained();
}

/* A denied/failed owned-bank erase must not be mistaken for reset success. */
static void test_settings_reset_failure_never_reboots(void) {
    start();
    erase_result = -EBUSY;
    assert(gmk_recovery_request(GMK_RECOVERY_SETTINGS_RESET) == -EBUSY);
    assert(erases == 1 && resets == 0 && magic_writes == 0);
    erase_result = -EIO;
    assert(gmk_recovery_request(GMK_RECOVERY_SETTINGS_RESET) == -EIO);
    assert(erases == 2 && resets == 0);
    assert(magic() == 0x5a5a5a5aU);
    check_retained();
}

static void test_settings_reset_success_uses_native_erase_and_reboot(void) {
    start();
    if (!setjmp(reset_jump)) {
        (void)gmk_recovery_request(GMK_RECOVERY_SETTINGS_RESET);
        assert(false);
    }
    assert(erases == 1 && resets == 1 && reset_type == SYS_REBOOT_WARM);
    assert(magic() == 0);
    check_retained();
}

static void test_recovery_rejects_irq_and_unknown_actions(void) {
    start();
    assert(gmk_recovery_request(GMK_RECOVERY_NONE) == -EINVAL);
    assert(gmk_recovery_request((enum gmk_recovery_action)4) == -EINVAL);
    in_isr = true;
    assert(gmk_recovery_request(GMK_RECOVERY_SETTINGS_RESET) == -EWOULDBLOCK);
    assert(erases == 0 && resets == 0 && magic_writes == 0);
}

/* Releasing/changing a startup chord must never fall through to another action. */
static void test_startup_chord_is_deliberate_and_one_shot(void) {
    struct gmk_recovery_startup state = {0};
    assert(gmk_recovery_startup_sample(&state, true, true, 10) == GMK_RECOVERY_NONE);
    assert(gmk_recovery_startup_sample(&state, true, true, 3009) == GMK_RECOVERY_NONE);
    assert(gmk_recovery_startup_sample(&state, true, true, 3010) == GMK_RECOVERY_SETTINGS_RESET);
    assert(gmk_recovery_startup_sample(&state, true, true, 4000) == GMK_RECOVERY_NONE);
    state = (struct gmk_recovery_startup){0};
    assert(gmk_recovery_startup_sample(&state, true, true, 0) == GMK_RECOVERY_NONE);
    assert(gmk_recovery_startup_sample(&state, true, false, 2000) == GMK_RECOVERY_NONE);
    assert(gmk_recovery_startup_sample(&state, true, false, 4000) == GMK_RECOVERY_NONE);
    state = (struct gmk_recovery_startup){0};
    assert(gmk_recovery_startup_sample(&state, false, false, 0) == GMK_RECOVERY_NONE);
    assert(gmk_recovery_startup_sample(&state, true, false, 251) == GMK_RECOVERY_NONE);
    assert(gmk_recovery_startup_sample(&state, true, false, 4000) == GMK_RECOVERY_NONE);
}

static void test_startup_hold_handles_clock_wrap_and_switch_settling(void) {
    struct gmk_recovery_startup state = {0};
    assert(gmk_recovery_startup_sample(&state, false, false, UINT32_MAX - 100) == GMK_RECOVERY_NONE);
    assert(gmk_recovery_startup_sample(&state, true, false, UINT32_MAX - 90) == GMK_RECOVERY_NONE);
    assert(gmk_recovery_startup_sample(&state, true, false, 2908) == GMK_RECOVERY_NONE);
    assert(gmk_recovery_startup_sample(&state, true, false, 2909) == GMK_RECOVERY_BOOTLOADER);
}

int main(void) {
    test_pinned_native_bootloader_behavior();
    test_pinned_native_normal_reset_behavior();
    test_settings_reset_failure_never_reboots();
    test_settings_reset_success_uses_native_erase_and_reboot();
    test_recovery_rejects_irq_and_unknown_actions();
    test_startup_chord_is_deliberate_and_one_shot();
    test_startup_hold_handles_clock_wrap_and_switch_settling();
    puts("Native reset and GMK70 recovery tests passed");
    return 0;
}

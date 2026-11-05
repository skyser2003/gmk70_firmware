# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile pinned native PM bookkeeping with only the device hardware replaced."""

import sys
from pathlib import Path

from test_nkro_boot import NkroBootTests


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    source = (root / "dependencies/zmk/app/src/pm.c").read_text()
    prelude = """
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <zmk/pm.h>
struct device { const char *name; unsigned index; };
static const struct device devices[] = {{"first",0}, {"second",1}};
static const struct device *pm_slots[2];
static size_t zmk_num_susp;
extern bool test_native_pm_suspended[2];
extern unsigned test_pm_suspend_calls, test_pm_resume_calls;
extern bool test_pm_fail, test_pm_inject_activity;
extern void gmk_power_note_activity(void);
#define TYPE_SECTION_START(name) pm_slots
#define LOG_ERR(...)
enum { PM_DEVICE_ACTION_SUSPEND, PM_DEVICE_ACTION_RESUME };
static size_t z_device_get_all_static(const struct device **out) { *out = devices; return 2; }
static bool device_is_ready(const struct device *dev) { (void)dev; return true; }
static bool pm_device_is_busy(const struct device *dev) { (void)dev; return false; }
static bool pm_device_wakeup_is_enabled(const struct device *dev) { (void)dev; return false; }
static bool pm_device_runtime_is_enabled(const struct device *dev) { (void)dev; return false; }
static int pm_device_action_run(const struct device *dev, int action) {
    bool suspending = action == PM_DEVICE_ACTION_SUSPEND;
    if (suspending && test_pm_fail && dev->index == 0) return -EIO;
    if (suspending && test_pm_inject_activity) { test_pm_inject_activity = false; gmk_power_note_activity(); }
    if (test_native_pm_suspended[dev->index] == suspending) return -EALREADY;
    test_native_pm_suspended[dev->index] = suspending;
    if (suspending) ++test_pm_suspend_calls; else ++test_pm_resume_calls;
    return 0;
}
"""
    functions = [
        NkroBootTests.function(source, "int zmk_pm_suspend_devices(void)"),
        NkroBootTests.function(source, "void zmk_pm_resume_devices(void)"),
    ]
    Path(sys.argv[1]).write_text(prelude + "\n".join(functions))
    activity_source = (root / "dependencies/zmk/app/src/activity.c").read_text()
    activity_prelude = """
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zmk/activity.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/pm.h>
#include <zmk/usb.h>
/* Pinned activity work callbacks leave their Zephyr work parameter unused. */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define CONFIG_ZMK_SLEEP 1
#define CONFIG_USB_DEVICE_STACK 1
#define MAX_IDLE_MS 30000
#define MAX_SLEEP_MS 60000
#define LOG_ERR(...)
static enum zmk_activity_state activity_state;
static uint32_t activity_last_uptime;
"""
    activity_functions: list[str] = []
    for signature in (
        "bool is_usb_power_present(void)",
        "int raise_event(void)",
        "int set_state(enum zmk_activity_state state)",
        "enum zmk_activity_state zmk_activity_get_state(void)",
        "void activity_work_handler(struct k_work *work)",
    ):
        activity_functions.append(NkroBootTests.function(activity_source, signature))
    Path(sys.argv[2]).write_text(activity_prelude + "\n".join(activity_functions))


if __name__ == "__main__":
    main()

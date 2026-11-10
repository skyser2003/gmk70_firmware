"""Reject incomplete native power and board-control integration in Studio images."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
import check_image


class BasicParityImageTests(unittest.TestCase):
    def test_native_power_path_and_peripheral_adapters_are_mandatory(self) -> None:
        symbols = {
            "gmk_controls_poll",
            "gmk_controls_project_modifiers",
            "gmk_power_poll",
            "gmk_power_receive",
            "gmk_power_core_decide",
            "gmk_power_coordinator_step",
            "__wrap_zmk_pm_suspend_devices",
            "__wrap_zmk_pm_resume_devices",
            "__wrap_zmk_pm_soft_off",
            "__wrap_zmk_usb_get_conn_state",
            "zmk_pm_suspend_devices",
            "z_sys_poweroff",
            "wb32_stop_hardware",
            "gmk_wired_power_prepare",
            "gmk_lighting_power_quiesce",
            "usb_dc_wakeup_request",
            "__wrap_zmk_endpoint_send_mouse_report",
            "gmk_recovery_startup_poll",
            "__wrap_zmk_settings_erase",
        }
        check_image.validate_basic_parity_linking(symbols)
        for symbol in symbols - {"gmk_controls_poll"}:
            with self.subTest(missing=symbol), self.assertRaises(ValueError):
                check_image.validate_basic_parity_linking(symbols - {symbol})
        with self.assertRaises(ValueError):
            check_image.validate_basic_parity_linking(symbols | {"input_thread_id"})
        with self.assertRaises(ValueError):
            check_image.validate_basic_parity_linking(
                symbols | {"__wrap_raise_zmk_activity_state_changed"}
            )

    def test_nonstudio_image_has_no_parity_requirement(self) -> None:
        check_image.validate_basic_parity_linking({"gmk_wired_scan"})


class RetainedSleepImageTests(unittest.TestCase):
    def test_complete_retained_path_and_native_clock_required(self) -> None:
        symbols = {
            "gmk_retained_sleep",
            "gmk_retained_poll",
            "__wrap_sys_clock_cycle_get_32",
            "__wrap_sys_clock_cycle_get_64",
            "sys_clock_cycle_get_64",
            "sys_clock_set_timeout",
            "sys_clock_announce",
            "wb32_clock_resume",
            "note_activity_work",
        }
        self.assertTrue(check_image.validate_retained_sleep_linking(symbols))
        for symbol in symbols:
            with self.subTest(missing=symbol), self.assertRaises(ValueError):
                check_image.validate_retained_sleep_linking(symbols - {symbol})

    def test_reset_only_path_remains_supported(self) -> None:
        self.assertFalse(
            check_image.validate_retained_sleep_linking({"wb32_stop_hardware"})
        )


class RetainedClockImageTests(unittest.TestCase):
    assembly = """00001000 <sys_clock_announce>:
 1000: f000 f800 bl 1200 <set_timeout>
00001100 <gmk_retained_sleep>:
 1100: f000 f800 bl 1300 <sys_clock_set_timeout>
00001200 <set_timeout>:
 1200: f000 b800 b.w 1300 <sys_clock_set_timeout>
00001300 <sys_clock_set_timeout>:
 1300: 6001 str r1, [r0, #0]
"""

    def test_actual_setter_and_both_paths(self) -> None:
        check_image.validate_retained_clock_linking(
            {"sys_clock_set_timeout": "T"}, self.assembly
        )

    def test_missing_weak_empty_or_bypassed_setter_rejected(self) -> None:
        for kind in ("W", "w", "t", ""):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                check_image.validate_retained_clock_linking(
                    {"sys_clock_set_timeout": kind}, self.assembly
                )
        for old in (
            "6001 str r1, [r0, #0]",
            "bl 1200 <set_timeout>",
            "bl 1300 <sys_clock_set_timeout>",
        ):
            with self.subTest(removed=old), self.assertRaises(ValueError):
                check_image.validate_retained_clock_linking(
                    {"sys_clock_set_timeout": "T"},
                    self.assembly.replace(old, "4770 bx lr"),
                )


if __name__ == "__main__":
    unittest.main()

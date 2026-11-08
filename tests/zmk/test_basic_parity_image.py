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


if __name__ == "__main__":
    unittest.main()

"""Canonical updater images require native framing and exclude legacy relay."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
import check_image


class NativeUpdateImageTests(unittest.TestCase):
    native: set[str] = {
        "gmk_native_update_exchange",
        "gmk_native_update_serve",
        "zmk_split_wired_get_item",
        "crc32_ieee",
    }

    def test_canonical_requires_native_parser_and_excludes_legacy(self) -> None:
        check_image.validate_native_update_linking(self.native, "wired-studio-updater")
        for missing in self.native:
            with self.subTest(missing=missing), self.assertRaises(ValueError):
                check_image.validate_native_update_linking(
                    self.native - {missing}, "wired-studio-updater"
                )
        for legacy in ("gmk_protocol_update", "gmk_native_update_bridge_exchange"):
            with self.subTest(legacy=legacy), self.assertRaises(ValueError):
                check_image.validate_native_update_linking(
                    self.native | {legacy}, "wired-studio-updater"
                )

    def test_only_explicit_bridge_may_link_both_carriers(self) -> None:
        both = self.native | {
            "gmk_protocol_update",
            "gmk_native_update_bridge_exchange",
        }
        check_image.validate_native_update_linking(both, "wired-studio-native-bridge")
        with self.assertRaises(ValueError):
            check_image.validate_native_update_linking(
                self.native, "wired-studio-native-bridge"
            )

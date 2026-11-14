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

    def test_shared_crc_requires_real_parser_transmitter_and_wrapper_calls(
        self,
    ) -> None:
        shared = (self.native - {"crc32_ieee"}) | {"__wrap_crc32_ieee", "gmk_crc32"}
        assembly = """08002300 <__wrap_crc32_ieee>:
 8002300: f000 b800 b.w 8002400 <gmk_crc32>
08002500 <zmk_split_wired_get_item>:
 8002500: f000 f800 bl 8002300 <__wrap_crc32_ieee>
08002600 <write_frame.constprop.0>:
 8002600: f000 f800 bl 8002300 <__wrap_crc32_ieee>
"""
        check_image.validate_native_update_linking(
            shared, "wired-studio-updater", assembly
        )
        for missing in shared:
            with self.subTest(missing=missing), self.assertRaises(ValueError):
                check_image.validate_native_update_linking(
                    shared - {missing}, "wired-studio-updater", assembly
                )
        for broken in (
            "",
            assembly.replace("b.w 8002400", "ldr 8002400"),
            assembly.replace("bl 8002300", "ldr 8002300", 1),
            assembly.replace("<write_frame.constprop.0>", "<unrelated>"),
        ):
            with self.subTest(broken=broken), self.assertRaises(ValueError):
                check_image.validate_native_update_linking(
                    shared, "wired-studio-updater", broken
                )

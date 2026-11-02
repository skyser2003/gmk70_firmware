"""Contracts required before native split changes may be packaged."""

import re
import unittest
from pathlib import Path


class NativeSplitSourceTests(unittest.TestCase):
    root: Path = Path(__file__).resolve().parents[2]

    def test_one_image_uses_both_upstream_role_cores(self) -> None:
        cmake = (self.root / "firmware/zmk/CMakeLists.txt").read_text()
        config = (
            self.root / "firmware/zmk/boards/zuoya/gmk70_wired/gmk70_wired_defconfig"
        ).read_text()
        self.assertIn("CONFIG_ZMK_SPLIT=y", config)
        self.assertIn("CONFIG_ZMK_SPLIT_ROLE_CENTRAL=y", config)
        self.assertIn("src/split/peripheral.c", cmake)
        self.assertIn("active_transport=gmk_native_peripheral_transport", cmake)

    def test_native_encoder_matches_authoritative_hardware_pins(self) -> None:
        # Derive pins from the maintained generator, not a second pin list.
        import sys

        sys.path.insert(0, str(self.root / "firmware/zmk/tools"))
        import wired_pins

        header = wired_pins.generate(
            self.root / "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json"
        )
        dts = (
            self.root / "firmware/zmk/boards/zuoya/gmk70_wired/gmk70_wired.dts"
        ).read_text()
        for axis in ("a", "b"):
            match = re.search(rf"gmk_encoder_{axis} = \{{(\d+), (\d+)\}}", header)
            self.assertIsNotNone(match)
            assert match is not None
            port, pin = int(match[1]), int(match[2])
            self.assertRegex(dts, rf"{axis}-gpios = <&gpio{chr(97 + port)} {pin} ")
        self.assertIn("zephyr,deferred-init;", dts)

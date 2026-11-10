"""Require state/priority preserving reuse of native initialization's stack."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
import check_image

ASSEMBLY = """00001000 <bg_thread_main>:
 1000: f000 f800 bl 1100 <__wrap_main>
00001100 <__wrap_main>:
 1100: f000 f800 bl 1200 <main>
 1104: f000 f800 bl 1300 <z_impl_k_thread_priority_set>
 1108: f000 f800 bl 1400 <gmk_scan_run>
"""


class IdleEntryTests(unittest.TestCase):
    def test_native_init_priority_then_scanning(self) -> None:
        check_image.validate_idle_main(ASSEMBLY)

    def test_bypassed_init_priority_or_scanner_is_rejected(self) -> None:
        for name in (
            "main",
            "z_impl_k_thread_priority_set",
            "gmk_scan_run",
            "__wrap_main",
        ):
            with self.subTest(name=name), self.assertRaises(ValueError):
                check_image.validate_idle_main(
                    ASSEMBLY.replace(f"<{name}>\n", "<other>\n")
                )

    def test_scanning_before_initialization_is_rejected(self) -> None:
        invalid = (
            ASSEMBLY.replace("<main>", "<temporary>")
            .replace("<gmk_scan_run>", "<main>")
            .replace("<temporary>", "<gmk_scan_run>")
        )
        with self.assertRaises(ValueError):
            check_image.validate_idle_main(invalid)

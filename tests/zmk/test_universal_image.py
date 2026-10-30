"""Universal packages require a linked runtime role guard before storage mount."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
import check_image


class UniversalImageTests(unittest.TestCase):
    assembly = """08002100 <settings_backend_init>:
 8002100: f000 f87e bl 8002200 <gmk_wired_is_left>
 8002104: b108 cbz r0, 8002120 <settings_backend_init+0x20>
 8002106: f000 f87e bl 8002300 <gmk_settings_store_mount>
 800210a: 4770 bx lr
 8002120: 4770 bx lr
"""

    def test_mount_requires_role_check_and_conditional_skip(self) -> None:
        check_image.validate_studio_roles(self.assembly)
        for broken in (
            self.assembly.replace("<gmk_wired_is_left>", "<other>"),
            self.assembly.replace("cbz", "b"),
            self.assembly.replace("b108 cbz r0,", "4770 bx lr ; cbz r0,"),
            self.assembly.replace("<settings_backend_init>", "<other>"),
        ):
            with self.subTest(broken=broken), self.assertRaises(ValueError):
                check_image.validate_studio_roles(broken)

    def test_inverted_unrelated_or_mount_reaching_conditions_are_rejected(self) -> None:
        for broken in (
            self.assembly.replace("cbz r0", "cbnz r0"),
            self.assembly.replace("cbz r0", "cbz r1"),
            self.assembly.replace(
                "8002120 <settings_backend_init+0x20>",
                "8002106 <settings_backend_init+0x6>",
            ),
            self.assembly.replace(
                "8002120: 4770 bx lr",
                "8002120: e7f1 b.n 8002106 <settings_backend_init+0x6>",
            ),
            self.assembly.replace(
                "8002120: 4770 bx lr",
                "8002120: f000 f87e bl 8002300 <gmk_settings_store_mount>",
            ),
            self.assembly.replace(
                "8002106: f000 f87e bl",
                "8002106: e00b b.n 8002120 <settings_backend_init+0x20>\n 8002108: f000 f87e bl",
            ),
        ):
            with self.subTest(broken=broken), self.assertRaises(ValueError):
                check_image.validate_studio_roles(broken)


if __name__ == "__main__":
    unittest.main()

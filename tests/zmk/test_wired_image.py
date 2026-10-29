"""Memory contract regressions independent of the Zephyr build configuration."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
import check_image


class WiredImageTests(unittest.TestCase):
    def test_permanent_vectors_belong_to_application_slot(self) -> None:
        image = struct.pack("<II", 0x20006FC0, 0x08002201) + bytes(1016)
        self.assertEqual(
            check_image.validate_vectors(image, 0x08002000)["bytes"], len(image)
        )

    def test_wrong_link_range_and_incomplete_image_end_are_rejected(self) -> None:
        base = 0x08002000
        image = struct.pack("<16I", 0x20006FC0, *([base + 0x201] * 15)) + bytes(960)
        data = bytearray(0x100 + len(image))
        data[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<H", data, 18, 40)
        struct.pack_into("<II", data, 24, base + 0x201, 52)
        struct.pack_into("<HH", data, 42, 32, 1)
        struct.pack_into("<8I", data, 52, 1, 0x100, base, base, 1024, 1024, 5, 4)
        data[0x100:] = image
        check_image.validate_wired_elf(bytes(data), image, base, base + 1024)
        with self.assertRaisesRegex(ValueError, "image.end"):
            check_image.validate_wired_elf(bytes(data), image, base, base + 1000)
        struct.pack_into("<I", data, 52 + 12, 0x08000000)
        with self.assertRaises(ValueError):
            check_image.validate_wired_elf(bytes(data), image, base, base + 1024)

    def test_studio_limit_is_independent_of_common_resident_abi(self) -> None:
        base = 0x08002000
        for size in (0x1A000, 0x1A004):
            image = struct.pack("<16I", 0x20006FC0, *([base + 0x201] * 15)) + bytes(
                size - 64
            )
            data = bytearray(0x100 + size)
            data[:7] = b"\x7fELF\x01\x01\x01"
            struct.pack_into("<H", data, 18, 40)
            struct.pack_into("<II", data, 24, base + 0x201, 52)
            struct.pack_into("<HH", data, 42, 32, 1)
            struct.pack_into("<8I", data, 52, 1, 0x100, base, base, size, size, 5, 4)
            data[0x100:] = image
            check_image.validate_wired_elf(bytes(data), image, base, base + size)
            if size == 0x1A000:
                check_image.validate_wired_elf(
                    bytes(data), image, base, base + size, 0x1A000
                )
            else:
                with self.assertRaises(ValueError):
                    check_image.validate_wired_elf(
                        bytes(data), image, base, base + size, 0x1A000
                    )

    def test_combined_physical_layout_and_stock_fn(self) -> None:
        import json
        import re

        root = Path(__file__).resolve().parents[2]
        board = root / "firmware/zmk/boards/zuoya/gmk70_wired"
        source = json.loads(
            (
                root / "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json"
            ).read_text()
        )
        coords = [tuple(key["matrix"]) for key in source["layouts"]["LAYOUT"]["layout"]]
        mapped = [
            (int(r), int(c))
            for r, c in re.findall(
                r"RC\((\d+),(\d+)\)", (board / "gmk70_wired.dts").read_text()
            )
        ]
        self.assertEqual(mapped, coords)
        bindings = re.findall(
            r"&(?:kp\s+\w+|(?:mo|to)\s+\d+|rgb_ug\s+\w+|none|trans)",
            (board / "gmk70_wired.keymap").read_text(),
        )
        self.assertEqual(len(bindings), 4 * len(coords))
        self.assertEqual(bindings[coords.index((4, 2))], "&mo 1")


if __name__ == "__main__":
    unittest.main()

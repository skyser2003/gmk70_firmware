"""Memory contract regressions independent of the Zephyr build configuration."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
import check_image


class WiredImageTests(unittest.TestCase):
    def test_actual_picker_table_and_native_load_boundaries(self) -> None:
        base = 0x08002000
        symbols = {
            "_zmk_behavior_ref_list_start": base,
            "_zmk_behavior_ref_list_end": base + 32,
            "__init_gmk70_menu_metadata_init": base + 128,
            "gmk70_menu_metadata_init": base + 224,
            "settings_handler_gmk_controls_bindings": base + 136,
            "zmk_behavior_find_behavior_name_from_local_id": base + 228,
            "gmk_native_behavior_find_behavior_name_from_local_id": base + 232,
            "__wrap_zmk_behavior_find_behavior_name_from_local_id": base + 236,
        }
        image = bytearray(256)
        for index, name in enumerate(
            (b"gmk_controls", b"gmk_sleep", b"gmk_radio", b"gmk_battery")
        ):
            struct.pack_into("<II", image, index * 8, base + 32 + index * 4, 0)
            struct.pack_into("<I", image, 32 + index * 4, base + 48 + index * 16)
            image[48 + index * 16 : 48 + index * 16 + len(name)] = name
        struct.pack_into("<I", image, 128, symbols["gmk70_menu_metadata_init"] | 1)
        struct.pack_into("<I", image, 140, 1)
        resolver = "zmk_behavior_find_behavior_name_from_local_id"
        assembly = "\n".join(
            f"{index:08x} <{caller}>:\n  100: f000 b000 b.w 200 <{target}>\n"
            for index, (caller, target) in enumerate(
                (
                    (resolver, "gmk_native_behavior_find_behavior_name_from_local_id"),
                    (f"__wrap_{resolver}", resolver),
                    ("keymap_handle_set", f"__wrap_{resolver}"),
                    ("keymap_handle_commit", f"__wrap_{resolver}"),
                )
            )
        )
        check_image.validate_merged_controls(symbols, bytes(image), base, assembly)
        duplicate = image.copy()
        duplicate[64:80] = b"gmk_flip\0".ljust(16, b"\0")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            check_image.validate_merged_controls(
                symbols, bytes(duplicate), base, assembly
            )
        with self.assertRaisesRegex(ValueError, "compatibility bypassed"):
            check_image.validate_merged_controls(
                symbols, bytes(image), base, assembly.replace("<__wrap_", "<bypassed_")
            )

    def test_static_profile_rejects_new_users_of_removed_facilities(self) -> None:
        config = "CONFIG_GMK70_MERGED_CONTROLS=y\nCONFIG_PM_DEVICE=y\n"
        check_image.validate_static_studio_config(config, "", set())
        for dts, symbols in (
            ("power-domains = <&domain>;", set()),
            ("", {"settings_register"}),
            ("", {"k_sched_time_slice_set"}),
        ):
            with self.subTest(dts=dts, symbols=symbols), self.assertRaises(ValueError):
                check_image.validate_static_studio_config(config, dts, symbols)

    def test_permanent_builds_reject_watchdog_and_outgoing_handoff_symbols(
        self,
    ) -> None:
        check_image.validate_permanent_symbols({"main", "gmk_zmk_receiver_step"})
        for name in (
            "gmk_zmk_watchdog_init",
            "gmk_zmk_watchdog_feed",
            "gmk_zmk_resident_handoff",
            "gmk_zmk_resident_branch",
        ):
            with self.subTest(name=name), self.assertRaises(ValueError):
                check_image.validate_permanent_symbols({"main", name})

    def test_handoff_capability_must_be_at_actual_image_end(self) -> None:
        marker = b"GMK70ZMKBOOT\x01\0\0\0"
        for enabled in (True, False):
            for payload in (
                bytes(1024),
                bytes(1008) + marker,
                bytes(1008) + marker + bytes(4),
            ):
                expected = enabled and payload.endswith(marker)
                if expected or (not enabled and not payload.endswith(marker)):
                    check_image.validate_handoff_payload(payload, enabled)
                else:
                    with self.assertRaises(ValueError):
                        check_image.validate_handoff_payload(payload, enabled)

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
        self.assertEqual(mapped, coords + [(10, 0), (10, 1)])
        bindings = re.findall(
            r"GMK70_NKRO_BINDING|&(?:kp\s+\w+|(?:mo|to|gmk_nkro)\s+\d+|rgb_ug\s+\w+|none|trans)",
            "\n".join(
                line
                for line in (board / "gmk70_wired.keymap").read_text().splitlines()
                if not line.lstrip().startswith("#")
            ),
        )
        self.assertEqual(len(bindings), 4 * (len(coords) + 2))
        self.assertEqual(bindings[coords.index((4, 2))], "&mo 1")
        for layer in (1, 3):
            self.assertEqual(
                bindings[layer * (len(coords) + 2) + coords.index((3, 5))],
                "GMK70_NKRO_BINDING",
            )


if __name__ == "__main__":
    unittest.main()

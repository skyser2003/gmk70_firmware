"""Direct boot must retain settings and exclude all upload entry points."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
import check_image


class StandaloneImageTests(unittest.TestCase):
    required = {
        "soc_reset_hook",
        "_k_thread_data_service_worker",
        "gmk_zmk_storage_claim",
        "gmk_zmk_service_idle",
    }

    def test_missing_boot_or_storage_support_is_rejected(self) -> None:
        check_image.validate_standalone_linking(self.required)
        for name in self.required:
            with self.subTest(name=name), self.assertRaises(ValueError):
                check_image.validate_standalone_linking(self.required - {name})

    def test_updater_entry_points_cannot_survive_standalone_link(self) -> None:
        for name in (
            "gmk_zmk_receiver_step",
            "gmk_zmk_raw_hid_init",
            "gmk_flash_page",
            "gmk_install_pending",
            "gmk_running_size",
            "gmk_wired_update",
            "gmk_native_update_serve",
            "__wrap_usb_hid_register_device",
            "gmk_trial_packet_valid",
            "gmk_boot_reset",
            "gmk_update_process",
        ):
            with self.subTest(name=name), self.assertRaises(ValueError):
                check_image.validate_native_update_linking(
                    self.required | {name}, "wired-studio-standalone"
                )

    def test_settings_boundary_and_direct_boot_vectors(self) -> None:
        prefix = struct.pack("<16I", 0x20006FC0, *([0x08000201] * 15))
        for size in (1024, 0x1C000, 0x1C004):
            image = prefix + bytes(size - len(prefix))
            elf = bytearray(0x100 + size)
            elf[:7] = b"\x7fELF\x01\x01\x01"
            struct.pack_into("<H", elf, 18, 40)
            struct.pack_into("<II", elf, 24, 0x08000201, 52)
            struct.pack_into("<HH", elf, 42, 32, 1)
            struct.pack_into(
                "<8I", elf, 52, 1, 0x100, 0x08000000, 0x08000000, size, size, 5, 4
            )
            elf[0x100:] = image
            if size <= 0x1C000:
                check_image.validate_wired_elf(
                    bytes(elf), image, 0x08000000, 0x08000000 + size, 0x1C000
                )
            else:
                with self.assertRaisesRegex(ValueError, "Studio settings"):
                    check_image.validate_wired_elf(
                        bytes(elf), image, 0x08000000, 0x08000000 + size, 0x1C000
                    )
        # A direct-boot image cannot be mistaken for an application-slot image.
        with self.assertRaises(ValueError):
            check_image.validate_vectors(prefix + bytes(1024), 0x08002000)
        with self.assertRaises(ValueError):
            check_image.validate_handoff_payload(
                prefix + b"GMK70ZMKBOOT\x01\0\0\0", False
            )

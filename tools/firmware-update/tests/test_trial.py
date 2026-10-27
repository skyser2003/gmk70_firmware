"""Offline trials must never become ordinary install packages or open HID."""

import hashlib
import json
import struct
import tempfile
import unittest
from pathlib import Path

from trial import TrialOp, TrialPackage, upload_packets
from update import Package


def image() -> bytes:
    data = bytearray(1284)
    struct.pack_into("<IIII", data, 0, 0x20006FC0, *([0x08020201] * 3))
    return bytes(data)


class TrialTests(unittest.TestCase):
    def test_package_round_trip_and_install_rejection(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = TrialPackage.create(image(), "right")
            package.write(root)
            self.assertEqual(TrialPackage.load(root), package)
            self.assertEqual(
                package.manifest["sha256"], hashlib.sha256(image()).hexdigest()
            )
            with self.assertRaises(FileNotFoundError):
                Package.load(root)
            # Even relabeled and given a coherent ordinary manifest, staging
            # vectors fail the existing normal updater before any device opens.
            (root / "application.bin").write_bytes(package.image)
            (root / "resident.bin").write_bytes(bytes(64))
            import zlib

            boot = bytes(64).ljust(0x2000, b"\xff")
            (root / "update.json").write_text(
                json.dumps(
                    {
                        "format": 1,
                        "board": "zuoya/gmk70",
                        "mcu": "WB32FQ95-256K",
                        "profile": "vial",
                        "app_address": 0x08002000,
                        "app_size": len(package.image),
                        "app_crc": zlib.crc32(package.image),
                        "app_sha256": hashlib.sha256(package.image).hexdigest(),
                        "boot_crc": zlib.crc32(boot),
                        "boot_sha256": hashlib.sha256(boot).hexdigest(),
                        "install_sha256": "",
                    }
                )
            )
            with self.assertRaisesRegex(ValueError, "link address"):
                Package.load(root)

    def test_new_memory_contract_rejects_legacy_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = TrialPackage.create(image(), "right")
            self.assertEqual(package.manifest["format"], "gmk70-staging-trial-v2")
            self.assertEqual(package.manifest["ram_start"], 0x20000040)
            package.write(root)
            manifest = dict(package.manifest)
            manifest["format"] = "gmk70-staging-trial-v1"
            (root / "trial.json").write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                TrialPackage.load(root)

    def test_manifest_corruption_and_identity_rejection(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = TrialPackage.create(image(), "right")
            package.write(root)
            for key in package.manifest:
                with self.subTest(key=key):
                    manifest: dict[str, object] = dict(package.manifest)
                    manifest[key] = "wrong"
                    (root / "trial.json").write_text(json.dumps(manifest))
                    with self.assertRaises(ValueError):
                        TrialPackage.load(root)
            package.write(root)
            (root / "trial.bin").write_bytes(image()[:-1] + b"\xff")
            with self.assertRaises(ValueError):
                TrialPackage.load(root)

    def test_vectors_bounds_and_reserved_ram(self) -> None:
        for size in (0, 64, 1020, 1025, 0x1E004):
            with self.subTest(size=size), self.assertRaises(ValueError):
                TrialPackage.create(bytes(size), "right")
        for offset, value in (
            (0, 0x20007000),
            (0, 0x20006FBC),
            (0, 0x20000000),
            (0, 0x20000040),
            (0, 0x20000020),
            (4, 0x08002201),
            (4, 0x08020200),
            (4, 0),
            (4, 0x08020001),
            (8, 0),
            (12, 0x08040001),
            (60, 0x08002201),
        ):
            data = bytearray(image())
            struct.pack_into("<I", data, offset, value)
            with (
                self.subTest(offset=offset, value=value),
                self.assertRaises(ValueError),
            ):
                TrialPackage.create(bytes(data), "right")
        with self.assertRaises(ValueError):
            TrialPackage.create(image(), "both")

    def test_packet_plan_has_no_install_or_run_and_binds_target(self) -> None:
        for target, role in (("left", 0), ("right", 1)):
            package = TrialPackage.create(image(), target)
            packets = upload_packets(package, 42)
            self.assertEqual(
                packets[0][:8], bytes((0x7F, 0x71, 2, role, TrialOp.BEGIN, 16, 0, 0))
            )
            self.assertEqual(
                struct.unpack_from("<IIII", packets[0], 16),
                (len(image()), package.manifest["crc32"], 0x08020000, 0x20006FC0),
            )
            self.assertEqual(packets[-1][4], TrialOp.SEAL)
            chunks = packets[1:-1]
            self.assertEqual(b"".join(p[16 : 16 + p[5]] for p in chunks), image())
            for index, packet in enumerate(chunks):
                self.assertEqual(struct.unpack_from("<II", packet, 8), (42, index * 16))
            self.assertTrue(
                all(len(p) == 32 and p[4] not in (5, 0x10) for p in packets)
            )
        for session in (0, -1, 2**32):
            with self.assertRaises(ValueError):
                upload_packets(TrialPackage.create(image(), "right"), session)


if __name__ == "__main__":
    unittest.main()

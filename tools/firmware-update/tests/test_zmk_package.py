import hashlib
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from test_update import FakeLink as OriginalFakeLink
from test_update import package as fixture_package

from update import ZMK_BOOT_CRC, ZMK_BOOT_SHA256, Op, Package, Updater
from zmk_package import build

ROOT = Path(__file__).resolve().parents[3]


def package(profile: str = "debug") -> Package:
    value = fixture_package(profile)
    if profile == "zmk":
        value.manifest["boot_crc"] = ZMK_BOOT_CRC
        value.manifest["boot_sha256"] = ZMK_BOOT_SHA256
    return value


class FakeLink(OriginalFakeLink):
    def exchange(self, packet: bytes) -> bytes:
        response = bytearray(super().exchange(packet))
        struct.pack_into("<I", response, 28, ZMK_BOOT_CRC)
        return bytes(response)


class ZmkPackageTests(unittest.TestCase):
    def test_offline_packaging_requires_validated_permanent_range_and_exact_resident(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "zmk.bin"
            image.write_bytes(package("zmk").image)
            resident = ROOT / "artifacts/qmk/debug-updater/resident.bin"
            report = root / "image-check.json"
            checked: dict[str, object] = {
                "board": "gmk70_wired",
                "variant": "wired-updater",
                "load_address": 0x08002000,
                "bytes": 1024,
                "image_end": 0x08002400,
                "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                "main_wrapper": "absent",
                "trial_backend": "absent",
            }
            report.write_text(json.dumps(checked))
            output = root / "package"
            build(image, resident, report, output)
            self.assertEqual(Package.load(output).manifest["profile"], "zmk")
            self.assertEqual(
                (output / "resident.bin").read_bytes(), resident.read_bytes()
            )
            self.assertFalse((output / "install.bin").exists())
            for key, value in (
                ("variant", "wired-qualification"),
                ("load_address", 0x08020000),
                ("image_end", 0x08002200),
                ("sha256", "0" * 64),
                ("main_wrapper", "present"),
            ):
                bad = checked | {key: value}
                report.write_text(json.dumps(bad))
                with self.subTest(key=key), self.assertRaises(ValueError):
                    build(image, resident, report, root / "bad")
                self.assertFalse((root / "bad").exists())
            report.write_text(json.dumps(checked))
            wrong = root / "wrong-resident.bin"
            wrong.write_bytes(bytes(64))
            with self.assertRaises(ValueError):
                build(image, wrong, report, root / "bad")

    def test_programmatic_staging_install_is_rejected_before_any_device_request(
        self,
    ) -> None:
        value = package("zmk")
        image = struct.pack("<II", 0x20006FC0, 0x08020201) + bytes(1016)
        manifest = value.manifest.copy()
        manifest["app_crc"] = zlib.crc32(image)
        manifest["app_sha256"] = hashlib.sha256(image).hexdigest()
        link = FakeLink()
        with self.assertRaises(ValueError):
            Updater(link).install(0, Package(image, manifest))
        self.assertEqual(link.operations, [])

    def test_zmk_accepts_only_pinned_resident_and_application_vectors(self) -> None:
        # The reviewed prerequisite is deliberately not rebuilt by this test.
        resident = (ROOT / "artifacts/qmk/debug-updater/resident.bin").read_bytes()
        boot = resident.ljust(0x2000, b"\xff")
        value = package("zmk")
        value.manifest["boot_crc"] = zlib.crc32(boot)
        value.manifest["boot_sha256"] = hashlib.sha256(boot).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "application.bin").write_bytes(value.image)
            (output / "resident.bin").write_bytes(resident)
            (output / "update.json").write_text(json.dumps(value.manifest))
            self.assertEqual(Package.load(output), value)
            for field in ("profile", "resident", "staging"):
                with self.subTest(field=field):
                    image = value.image
                    manifest = dict(value.manifest)
                    candidate = resident
                    if field == "profile":
                        manifest["profile"] = "wired-qualification"
                    elif field == "resident":
                        candidate = bytes(64)
                        fake = candidate.ljust(0x2000, b"\xff")
                        manifest["boot_crc"] = zlib.crc32(fake)
                        manifest["boot_sha256"] = hashlib.sha256(fake).hexdigest()
                    else:
                        image = struct.pack("<II", 0x20006FC0, 0x08020201) + bytes(1016)
                        manifest["app_crc"] = zlib.crc32(image)
                        manifest["app_sha256"] = hashlib.sha256(image).hexdigest()
                    (output / "application.bin").write_bytes(image)
                    (output / "resident.bin").write_bytes(candidate)
                    (output / "update.json").write_text(json.dumps(manifest))
                    with self.assertRaises(ValueError):
                        Package.load(output)

    def test_migration_zmk_update_and_vial_rollback_keep_right_first(self) -> None:
        for profile in ("zmk", "vial", "debug"):
            with self.subTest(profile=profile):
                from unittest.mock import patch

                link = FakeLink() if profile == "zmk" else OriginalFakeLink()
                with patch("update.time.sleep"):
                    Updater(link).install_both(package(profile))
                self.assertEqual(
                    [target for target, op in link.operations if op == Op.INSTALL],
                    [1, 0],
                )

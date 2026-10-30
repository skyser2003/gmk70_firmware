import hashlib
import json
import os
import struct
import tempfile
import unittest
import zlib
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import patch

from test_update import FakeLink as OriginalFakeLink
from test_update import package as fixture_package

import update
import zmk_package
from studio_rpc import JsonValue
from update import ZMK_BOOT_CRC, ZMK_BOOT_SHA256, Op, Package, Updater
from zmk_package import build

ROOT = Path(__file__).resolve().parents[3]
SYNTHETIC_RESIDENT = bytes(range(256)) * 2
SYNTHETIC_BOOT = SYNTHETIC_RESIDENT.ljust(0x2000, b"\xff")


@contextmanager
def synthetic_resident_identity() -> Iterator[None]:
    """Replace only the identity inputs, leaving validation/hash code real."""
    sha = hashlib.sha256(SYNTHETIC_BOOT).hexdigest()
    crc = zlib.crc32(SYNTHETIC_BOOT)
    with (
        patch.object(update, "ZMK_BOOT_SHA256", sha),
        patch.object(update, "ZMK_BOOT_CRC", crc),
        patch.object(zmk_package, "ZMK_BOOT_SHA256", sha),
        patch.object(zmk_package, "ZMK_BOOT_CRC", crc),
    ):
        yield


class ZmkFixtureIndependenceTests(unittest.TestCase):
    def test_mandatory_package_checks_work_without_ignored_resident(self) -> None:
        read_bytes = Path.read_bytes
        artifact = ROOT / "artifacts/qmk/debug-updater/resident.bin"
        artifact_reads: list[Path] = []

        def missing_artifact(path: Path) -> bytes:
            if path == artifact:
                artifact_reads.append(path)
                raise FileNotFoundError(
                    "Simulated initialized checkout without resident build output"
                )
            return read_bytes(path)

        cases = unittest.TestSuite(
            [
                ZmkPackageTests(
                    "test_offline_packaging_requires_validated_permanent_range_and_exact_resident"
                ),
                ZmkPackageTests(
                    "test_zmk_accepts_only_pinned_resident_and_application_vectors"
                ),
            ]
        )
        result = unittest.TestResult()
        with patch.object(
            Path, "read_bytes", autospec=True, side_effect=missing_artifact
        ):
            cases.run(result)
        self.assertTrue(result.wasSuccessful(), str(result.errors + result.failures))
        self.assertEqual(result.testsRun, 2)
        self.assertEqual(result.skipped, [])
        self.assertEqual(artifact_reads, [])


def package(profile: str = "debug") -> Package:
    value = fixture_package(profile)
    if profile in ("zmk", "zmk-studio", "zmk-studio-universal"):
        value.manifest["boot_crc"] = ZMK_BOOT_CRC
        value.manifest["boot_sha256"] = ZMK_BOOT_SHA256
    return value


class FakeLink(OriginalFakeLink):
    def exchange(self, packet: bytes) -> bytes:
        response = bytearray(super().exchange(packet))
        struct.pack_into("<I", response, 28, ZMK_BOOT_CRC)
        return bytes(response)


class ZmkPackageTests(unittest.TestCase):
    def test_universal_installs_identical_application_on_both_targets(self) -> None:
        candidate = package("zmk-studio-universal")
        link = FakeLink()
        with patch("update.time.sleep"):
            Updater(link).install_both(candidate)
        self.assertEqual(
            [target for target, op in link.operations if op == Op.INSTALL], [1, 0]
        )
        self.assertEqual(bytes(link.data[0]), candidate.image)
        self.assertEqual(bytes(link.data[1]), candidate.image)

    def test_universal_profile_requires_studio_bound_and_pinned_resident(self) -> None:
        value = package("zmk")
        value.manifest["profile"] = "zmk-studio-universal"
        value.validate_application()
        for corruption in ("resident", "range"):
            manifest = value.manifest.copy()
            image = value.image
            if corruption == "resident":
                manifest["boot_crc"] ^= 1
            else:
                image += bytes(0x1A004 - len(image))
                manifest["app_size"] = len(image)
                manifest["app_crc"] = zlib.crc32(image)
                manifest["app_sha256"] = hashlib.sha256(image).hexdigest()
            with self.subTest(corruption=corruption), self.assertRaises(ValueError):
                Package(image, manifest).validate_application()

    def test_only_role_validated_studio_report_emits_universal_package(self) -> None:
        with synthetic_resident_identity(), tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            image, resident, report = (
                root / name for name in ("app.bin", "boot.bin", "report.json")
            )
            payload = package("zmk").image
            image.write_bytes(payload)
            resident.write_bytes(SYNTHETIC_RESIDENT)
            checked: dict[str, JsonValue] = {
                "board": "gmk70_wired",
                "variant": "wired-studio-updater",
                "load_address": 0x08002000,
                "bytes": 1024,
                "image_end": 0x08002400,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "main_wrapper": "absent",
                "trial_backend": "absent",
                "runtime_roles": ["left", "right"],
            }
            report.write_text(json.dumps(checked))
            build(image, resident, report, root / "universal")
            self.assertEqual(
                Package.load(root / "universal").manifest["profile"],
                "zmk-studio-universal",
            )
            for roles in (["left"], ["right"], ["left", "right", "other"]):
                report.write_text(json.dumps(checked | {"runtime_roles": roles}))
                with self.subTest(roles=roles), self.assertRaises(ValueError):
                    build(image, resident, report, root / "bad")

    def test_production_pins_are_the_reviewed_resident_identity(self) -> None:
        expected_sha = (
            "be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a"
        )
        for module in (update, zmk_package):
            self.assertEqual(module.ZMK_BOOT_SHA256, expected_sha)
            self.assertEqual(module.ZMK_BOOT_CRC, 1770733249)
        value = package("zmk")
        value.manifest["boot_crc"] = zlib.crc32(SYNTHETIC_BOOT)
        value.manifest["boot_sha256"] = hashlib.sha256(SYNTHETIC_BOOT).hexdigest()
        with self.assertRaisesRegex(ValueError, "exact reviewed resident"):
            value.validate_application()

    @unittest.skipUnless(
        os.environ.get("GMK70_TEST_BUILT_ZMK_RESIDENT") == "1",
        "optional built-resident integration; set GMK70_TEST_BUILT_ZMK_RESIDENT=1",
    )
    def test_optional_built_resident_matches_production_pins(self) -> None:
        resident = (ROOT / "artifacts/qmk/debug-updater/resident.bin").read_bytes()
        self.assertTrue(64 <= len(resident) <= 0x2000)
        padded = resident.ljust(0x2000, b"\xff")
        self.assertEqual(hashlib.sha256(padded).hexdigest(), update.ZMK_BOOT_SHA256)
        self.assertEqual(zlib.crc32(padded), update.ZMK_BOOT_CRC)

    def test_offline_packaging_requires_validated_permanent_range_and_exact_resident(
        self,
    ) -> None:
        with synthetic_resident_identity(), tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "zmk.bin"
            image.write_bytes(package("zmk").image)
            resident = root / "synthetic-resident.bin"
            resident.write_bytes(SYNTHETIC_RESIDENT)
            report = root / "image-check.json"
            checked: dict[str, JsonValue] = {
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
            for variant in (
                "wired-studio-updater",
                "wired-studio-update-test",
                "wired-studio-radio-qualification",
                "wired-studio-handoff",
                "wired-studio-handoff-test",
                "wired-studio-watchdog-rollback",
                "wired-studio-watchdog-rollback-test",
            ):
                report.write_text(json.dumps(checked | {"variant": variant}))
                build(image, resident, report, root / variant)
                self.assertEqual(
                    Package.load(root / variant).manifest["profile"], "zmk-studio"
                )
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
        resident = SYNTHETIC_RESIDENT
        boot = SYNTHETIC_BOOT
        value = package("zmk")
        value.manifest["boot_crc"] = zlib.crc32(boot)
        value.manifest["boot_sha256"] = hashlib.sha256(boot).hexdigest()
        with synthetic_resident_identity(), tempfile.TemporaryDirectory() as directory:
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

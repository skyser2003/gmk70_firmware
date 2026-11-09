"""Verify the firmware bundles published by GitHub Actions."""

import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[2]
VARIANTS = {
    "qmk-default": ("qmk", "default", "normal"),
    "via-release": ("qmk", "release", "normal"),
    "via-debug": ("qmk", "debug", "normal"),
    "via-release-updater": ("qmk", "release", "updater"),
    "via-debug-updater": ("qmk", "debug", "updater"),
    "vial": ("vial", "vial", "normal"),
    "vial-updater": ("vial", "vial", "updater"),
    "vial-unlocked-updater": ("vial", "vial", "unlocked-updater"),
    "zmk": ("zmk", "wired-studio-updater", "normal"),
    "zmk-standalone": ("zmk", "wired-studio-standalone", "normal"),
}


class GitHubPackageTests(unittest.TestCase):
    def setUp(self) -> None:
        module = PROJECT / "tools/build-support/github_build.py"
        self.assertTrue(module.is_file(), "Missing GitHub firmware packager")
        spec = importlib.util.spec_from_file_location("github_build", module)
        assert spec is not None and spec.loader is not None
        self.build = importlib.util.module_from_spec(spec)
        with patch.object(sys, "path", [str(module.parent), *sys.path]):
            spec.loader.exec_module(self.build)
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def fixture(self, variant: str) -> Path:
        platform, keymap, mode = VARIANTS[variant]
        folder = keymap + (f"-{mode}" if mode != "normal" else "")
        output = self.root / "artifacts" / platform / folder
        output.mkdir(parents=True)
        target = f"zuoya_gmk70_{keymap}"
        names = [
            f"{target}.elf",
            f"{target}.map",
            "source.tar.gz",
            "sources.lock.json",
            "gmk70-source-origins.json",
            "build-info.txt",
            "cflags.txt",
            "ldflags.txt",
            "vial.json" if platform == "vial" else "ZUOYA_GMK70.json",
        ]
        if platform == "zmk":
            names = [
                "zmk.bin",
                "zmk.elf",
                "zmk.map",
                "zmk.config",
                "zephyr.dts",
                "compile_commands.json",
                "image-check.json",
                "source.tar.gz",
                "sources.lock.json",
                "build-info.txt",
            ]
            if variant == "zmk":
                names += [
                    "package-info.json",
                    "application.bin",
                    "resident.bin",
                    "install.bin",
                    "update.json",
                ]
        elif mode != "normal":
            names += [
                "application.bin",
                "resident.bin",
                "resident.elf",
                "resident.map",
                "install.bin",
                "update.json",
            ]
        else:
            names += [f"{target}.bin"]
        if platform in ("vial", "zmk"):
            names += [f"gmk70-{variant}.zip", "README.txt", "FIRMWARE-SHA256SUMS"]
        for name in names:
            (output / name).write_text(f"{variant}: {name}\n")
        if platform == "zmk":
            application = (
                output
                / ("zmk.bin" if variant == "zmk-standalone" else "application.bin")
            ).read_bytes()
            (output / "zmk.bin").write_bytes(application)
            (output / "image-check.json").write_text(
                json.dumps(
                    {
                        "board": "gmk70_wired",
                        "variant": keymap,
                        "updater": "absent"
                        if variant == "zmk-standalone"
                        else "present",
                        "runtime_roles": ["left", "right"],
                        "sha256": hashlib.sha256(application).hexdigest(),
                        "bytes": len(application),
                        "main_wrapper": "absent",
                        "trial_backend": "absent",
                    }
                )
            )
        manifest = "".join(
            f"{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}\n"
            for name in names
        )
        (output / "SHA256SUMS").write_text(manifest)
        (output / "build.log").write_text("build succeeded\n")
        return output

    def test_all_variants_publish_only_firmware_and_checksums(self) -> None:
        for variant, (platform, keymap, mode) in VARIANTS.items():
            with self.subTest(variant=variant):
                source = self.fixture(variant)
                dist = self.root / "dist"
                dist.mkdir(exist_ok=True)
                stem = (
                    "gmk70-vial-unlocked"
                    if variant == "vial-unlocked-updater"
                    else f"gmk70-{variant}"
                )
                for suffix in (".tar.gz", ".hex", ".zip"):
                    (dist / f"gmk70-{variant}{suffix}").write_text("obsolete")
                self.build.package_variant(self.root, variant)
                self.assertFalse((dist / f"gmk70-{variant}.tar.gz").exists())
                self.assertFalse((dist / f"gmk70-{variant}.hex").exists())
                if variant == "zmk" or mode != "normal":
                    with zipfile.ZipFile(dist / f"{stem}.zip") as archive:
                        self.assertEqual(
                            set(archive.namelist()),
                            {
                                "application.bin",
                                "install.bin",
                                "resident.bin",
                                "update.json",
                                "FIRMWARE-SHA256SUMS",
                            },
                        )
                        for name in (
                            "application.bin",
                            "install.bin",
                            "resident.bin",
                            "update.json",
                        ):
                            self.assertEqual(
                                archive.read(name), (source / name).read_bytes()
                            )
                        for line in (
                            archive.read("FIRMWARE-SHA256SUMS").decode().splitlines()
                        ):
                            digest, name = line.split("  ")
                            self.assertEqual(
                                hashlib.sha256(archive.read(name)).hexdigest(), digest
                            )
                else:
                    name = (
                        "zmk.bin" if platform == "zmk" else f"zuoya_gmk70_{keymap}.bin"
                    )
                    self.assertEqual(
                        (dist / f"{stem}.bin").read_bytes(),
                        (source / name).read_bytes(),
                    )
                    self.assertFalse((dist / f"{stem}.zip").exists())
                for line in (dist / f"{stem}.sha256").read_text().splitlines():
                    digest, name = line.split("  ", 1)
                    self.assertEqual(
                        hashlib.sha256((dist / name).read_bytes()).hexdigest(), digest
                    )
        self.assertFalse(list((self.root / "dist").glob("*.tar.gz")))
        self.assertFalse(list((self.root / "dist").glob("*unlocked-updater*")))

    def test_standalone_rejects_updater_artifacts_and_report(self) -> None:
        source = self.fixture("zmk-standalone")
        manifest = source / "SHA256SUMS"
        original = manifest.read_text()
        for name in (
            "application.bin",
            "resident.bin",
            "install.bin",
            "update.json",
            "package-info.json",
        ):
            (source / name).write_bytes(b"updater artifact")
            manifest.write_text(
                original
                + f"{hashlib.sha256(b'updater artifact').hexdigest()}  {name}\n"
            )
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(ValueError, "cannot contain updater"),
            ):
                self.build.package_variant(self.root, "zmk-standalone")
        report_path = source / "image-check.json"
        report = json.loads(report_path.read_text())
        report["updater"] = "present"
        report_path.write_text(json.dumps(report))
        manifest.write_text(
            "\n".join(
                f"{hashlib.sha256(report_path.read_bytes()).hexdigest()}  image-check.json"
                if line.endswith("  image-check.json")
                else line
                for line in original.splitlines()
            )
            + "\n"
        )
        with self.assertRaisesRegex(ValueError, "validated universal Studio"):
            self.build.package_variant(self.root, "zmk-standalone")
        self.assertFalse((self.root / "dist").exists())

    def test_zmk_rejects_noncanonical_role_or_trial_report(self) -> None:
        source = self.fixture("zmk")
        original = (source / "image-check.json").read_text()
        for field, value in (
            ("variant", "wired-studio-native-bridge"),
            ("runtime_roles", ["left"]),
            ("trial_backend", "present"),
            ("bytes", 0),
        ):
            report = json.loads(original)
            report[field] = value
            path = source / "image-check.json"
            path.write_text(json.dumps(report))
            lines = (source / "SHA256SUMS").read_text().splitlines()
            (source / "SHA256SUMS").write_text(
                "\n".join(
                    f"{hashlib.sha256(path.read_bytes()).hexdigest()}  image-check.json"
                    if line.endswith("  image-check.json")
                    else line
                    for line in lines
                )
                + "\n"
            )
            with (
                self.subTest(field=field),
                self.assertRaisesRegex(ValueError, "validated universal Studio"),
            ):
                self.build.package_variant(self.root, "zmk")
        self.assertFalse((self.root / "dist").exists())

    def test_zmk_release_requires_firmware_and_validation(self) -> None:
        source = self.fixture("zmk")
        manifest = source / "SHA256SUMS"
        original = manifest.read_text()
        for missing in (
            "application.bin",
            "update.json",
            "image-check.json",
            "install.bin",
        ):
            manifest.write_text(
                "\n".join(
                    line
                    for line in original.splitlines()
                    if not line.endswith(f"  {missing}")
                )
                + "\n"
            )
            with (
                self.subTest(missing=missing),
                self.assertRaisesRegex(ValueError, "Missing required"),
            ):
                self.build.package_variant(self.root, "zmk")

    def test_manifested_hex_is_rejected_before_packaging(self) -> None:
        source = self.fixture("via-release")
        obsolete = source / "zuoya_gmk70_release.hex"
        obsolete.write_bytes(b"old HEX firmware")
        with (source / "SHA256SUMS").open("a") as manifest:
            manifest.write(
                f"{hashlib.sha256(obsolete.read_bytes()).hexdigest()}  {obsolete.name}\n"
            )
        with self.assertRaisesRegex(ValueError, "HEX firmware"):
            self.build.package_variant(self.root, "via-release")
        self.assertFalse((self.root / "dist").exists())

    def test_unlocked_updater_requires_its_firmware(self) -> None:
        source = self.fixture("vial-unlocked-updater")
        manifest = source / "SHA256SUMS"
        manifest.write_text(
            "".join(
                line + "\n"
                for line in manifest.read_text().splitlines()
                if not line.endswith("  application.bin")
            )
        )
        with self.assertRaisesRegex(ValueError, "Missing required"):
            self.build.package_variant(self.root, "vial-unlocked-updater")

    def test_corruption_preserves_previous_bundle(self) -> None:
        source = self.fixture("via-release")
        dist = self.root / "dist"
        dist.mkdir()
        previous = dist / "gmk70-via-release.tar.gz"
        previous.write_text("previous verified package")
        (source / "zuoya_gmk70_release.bin").write_text("corrupt")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            self.build.package_variant(self.root, "via-release")
        self.assertEqual(previous.read_text(), "previous verified package")

    def test_source_archive_corruption_blocks_publication(self) -> None:
        source = self.fixture("vial")
        (source / "source.tar.gz").write_text("corrupt sources")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            self.build.package_variant(self.root, "vial")
        self.assertFalse((self.root / "dist").exists())

    def test_updater_requires_installation_and_update_metadata(self) -> None:
        source = self.fixture("via-debug-updater")
        manifest = source / "SHA256SUMS"
        manifest.write_text(
            "\n".join(
                line
                for line in manifest.read_text().splitlines()
                if not line.endswith("  update.json")
            )
            + "\n"
        )
        with self.assertRaisesRegex(ValueError, "Missing required"):
            self.build.package_variant(self.root, "via-debug-updater")

    def test_manifest_cannot_escape_artifact_directory(self) -> None:
        source = self.fixture("via-debug")
        outside = source.parent / "outside.bin"
        outside.write_text("not an artifact")
        digest = hashlib.sha256(outside.read_bytes()).hexdigest()
        (source / "SHA256SUMS").write_text(f"{digest}  ../outside.bin\n")
        with self.assertRaisesRegex(ValueError, "Invalid checksum entry"):
            self.build.package_variant(self.root, "via-debug")

    def test_failed_build_cannot_publish_stale_artifacts(self) -> None:
        self.fixture("via-release")
        script = self.root / "scripts/build.sh"
        script.parent.mkdir()
        script.write_text("#!/usr/bin/env bash\nexit 8\n")
        with self.assertRaises(subprocess.CalledProcessError):
            self.build.build_variant(self.root, "via-release")
        self.assertFalse((self.root / "dist").exists())

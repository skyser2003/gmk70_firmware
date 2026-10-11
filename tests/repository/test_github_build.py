"""Verify the firmware bundles published by GitHub Actions."""

import hashlib
import importlib.util
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
VARIANTS = {
    "qmk-default": ("qmk", "default", "normal"),
    "via-release": ("qmk", "release", "normal"),
    "via-debug": ("qmk", "debug", "normal"),
    "via-release-updater": ("qmk", "release", "updater"),
    "via-debug-updater": ("qmk", "debug", "updater"),
    "vial": ("vial", "vial", "normal"),
    "vial-updater": ("vial", "vial", "updater"),
}


class GitHubPackageTests(unittest.TestCase):
    def setUp(self) -> None:
        module = PROJECT / "tools/build-support/github_build.py"
        self.assertTrue(module.is_file(), "Missing GitHub firmware packager")
        spec = importlib.util.spec_from_file_location("github_build", module)
        assert spec is not None and spec.loader is not None
        self.build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.build)
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def fixture(self, variant: str) -> Path:
        platform, keymap, mode = VARIANTS[variant]
        folder = keymap + ("-updater" if mode == "updater" else "")
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
        if mode == "updater":
            names += [
                "application.bin",
                "application.hex",
                "resident.bin",
                "resident.elf",
                "resident.map",
                "install.bin",
                "install.hex",
                "update.json",
            ]
        else:
            names += [f"{target}.bin", f"{target}.hex"]
        if platform == "vial":
            names += [f"gmk70-{variant}.zip", "README.txt", "FIRMWARE-SHA256SUMS"]
        for name in names:
            (output / name).write_text(f"{variant}: {name}\n")
        manifest = "".join(
            f"{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}\n"
            for name in names
        )
        (output / "SHA256SUMS").write_text(manifest)
        (output / "build.log").write_text("build succeeded\n")
        return output

    def test_all_variants_have_distinct_complete_bundles(self) -> None:
        for variant, (platform, keymap, mode) in VARIANTS.items():
            with self.subTest(variant=variant):
                source = self.fixture(variant)
                # Stale, unmanifested files must not enter a release bundle.
                (source / "stale-image.bin").write_text("old firmware")
                self.build.package_variant(self.root, variant)
                dist = self.root / "dist"
                with tarfile.open(dist / f"gmk70-{variant}.tar.gz") as bundle:
                    names = bundle.getnames()
                    self.assertIn("source.tar.gz", names)
                    self.assertIn("SHA256SUMS", names)
                    self.assertIn("build.log", names)
                    self.assertNotIn("stale-image.bin", names)
                    if mode == "updater":
                        self.assertIn("install.bin", names)
                        self.assertIn("application.bin", names)
                        self.assertIn("update.json", names)
                    else:
                        self.assertEqual(
                            (dist / f"gmk70-{variant}.bin").read_bytes(),
                            (source / f"zuoya_gmk70_{keymap}.bin").read_bytes(),
                        )
                if platform == "vial":
                    self.assertEqual(
                        (dist / f"gmk70-{variant}.zip").read_bytes(),
                        (source / f"gmk70-{variant}.zip").read_bytes(),
                    )
                for line in (dist / f"gmk70-{variant}.sha256").read_text().splitlines():
                    digest, name = line.split("  ", 1)
                    self.assertEqual(
                        hashlib.sha256((dist / name).read_bytes()).hexdigest(), digest
                    )
        self.assertEqual(len(list((self.root / "dist").glob("*.tar.gz"))), 7)

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

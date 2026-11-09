"""Verify firmware preservation and change-only notes for release corrections."""

import importlib.util
import io
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[2]


class ReleaseFormatTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        source = PROJECT / "tools/build-support/repackage_release.py"
        spec = importlib.util.spec_from_file_location("repackage_release", source)
        assert spec is not None and spec.loader is not None
        self.repack = importlib.util.module_from_spec(spec)
        with patch.object(sys, "path", [str(source.parent), *sys.path]):
            spec.loader.exec_module(self.repack)

    def test_repacking_preserves_only_required_firmware_bytes(self) -> None:
        files = {
            name: bytes(range(256)) + name.encode() for name in self.repack.UPDATE_FILES
        }
        for kind in ("zip", "tar.gz"):
            with self.subTest(kind=kind):
                data = io.BytesIO()
                contents = dict(
                    files,
                    **{
                        "README.txt": b"instructions",
                        "source.tar.gz": b"sources",
                        "../escape": b"untrusted path",
                    },
                )
                if kind == "zip":
                    with zipfile.ZipFile(data, "w") as archive:
                        for name, payload in contents.items():
                            archive.writestr(name, payload)
                else:
                    with tarfile.open(fileobj=data, mode="w:gz") as archive:
                        for name, payload in contents.items():
                            item = tarfile.TarInfo(name)
                            item.size = len(payload)
                            archive.addfile(item, io.BytesIO(payload))
                output = self.root / kind
                output.mkdir()
                self.repack.unpack_firmware(data.getvalue(), f"firmware.{kind}", output)
                self.assertEqual(
                    {p.name: p.read_bytes() for p in output.iterdir()}, files
                )
                self.assertFalse((self.root / "escape").exists())

    def test_missing_firmware_stops_repack(self) -> None:
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            archive.writestr("application.bin", b"firmware")
        with self.assertRaisesRegex(ValueError, "Missing"):
            self.repack.unpack_firmware(data.getvalue(), "firmware.zip", self.root)

    def test_tar_links_cannot_replace_required_firmware(self) -> None:
        data = io.BytesIO()
        with tarfile.open(fileobj=data, mode="w:gz") as archive:
            item = tarfile.TarInfo("application.bin")
            item.type = tarfile.SYMTYPE
            item.linkname = "../outside.bin"
            archive.addfile(item)
        with self.assertRaisesRegex(ValueError, "Missing"):
            self.repack.unpack_firmware(data.getvalue(), "firmware.tar.gz", self.root)

    def test_notes_contain_only_changes_since_previous_family_tag(self) -> None:
        def git(*args: str) -> str:
            return subprocess.check_output(
                [
                    "git",
                    "-C",
                    str(self.root),
                    "-c",
                    "commit.gpgsign=false",
                    "-c",
                    "user.name=Test",
                    "-c",
                    "user.email=test@example.test",
                    *args,
                ],
                text=True,
            ).strip()

        git("init", "--quiet")
        git("commit", "--allow-empty", "-m", "Previous release")
        previous = git("rev-parse", "HEAD")
        git("commit", "--allow-empty", "-m", "Fix wake behavior")
        current = git("rev-parse", "HEAD")
        output = self.root / "notes.md"
        real_check_output = subprocess.check_output

        def checkout_command(args: list[str], **kwargs: object) -> str:
            return str(real_check_output(args, cwd=self.root, text=True))

        with patch.object(
            self.repack.subprocess, "check_output", side_effect=checkout_command
        ):
            self.repack.write_diff_notes(
                "zmk",
                "zmk-v0.1.2",
                current,
                [("zmk-v0.1.1", previous), ("vial-9.9.9", current)],
                output,
            )
        self.assertEqual(output.read_text(), f"- Fix wake behavior ({current[:7]})\n")

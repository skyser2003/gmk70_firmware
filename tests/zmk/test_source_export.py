"""Verify released ZMK sources rebuild from hashes without Git metadata."""

import json
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
import workspace


class SourceExportTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name) / "input"
        lock: workspace.SourceLock = {
            "zmk": {"repository": "https://example.test/zmk", "commit": "a" * 40},
            "zephyr": {"repository": "https://example.test/zephyr", "commit": "b" * 40},
            "zmk_modules": {
                "nanopb": {
                    "repository": "https://example.test/nanopb",
                    "commit": "c" * 40,
                    "path": "modules/lib/nanopb",
                }
            },
        }
        self.write("dependencies/sources.lock.json", json.dumps(lock))
        for directory, _ in workspace.sources(self.root):
            directory.mkdir(parents=True, exist_ok=True)
            (directory / "source.c").write_text("int source;\n")
            (directory / ".git").mkdir()
            (directory / ".git/private-config").write_text("must not be exported")
        self.link = self.root / "dependencies/modules/lib/nanopb/header.h"
        self.link.symlink_to("source.c")
        self.archive = Path(temporary.name) / "source.tar.gz"
        with tarfile.open(self.archive, "w:gz") as bundle:
            bundle.add(
                self.root / "dependencies/sources.lock.json",
                arcname="dependencies/sources.lock.json",
            )

    def write(self, name: str, content: str) -> None:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)

    def exported(self) -> Path:
        with patch.object(workspace, "verify") as verify:
            workspace.export(self.root, self.archive)
            verify.assert_called_once_with(self.root)
        output = self.root.parent / "output"
        with tarfile.open(self.archive) as bundle:
            self.assertFalse(
                any(".git" in Path(name).parts for name in bundle.getnames())
            )
            bundle.extractall(output, filter="data")
        return output

    def test_complete_sources_preserve_internal_links_and_verify_without_git(
        self,
    ) -> None:
        output = self.exported()
        link = output / "dependencies/modules/lib/nanopb/header.h"
        self.assertTrue(link.is_symlink())
        self.assertEqual(link.readlink(), Path("source.c"))
        workspace.verify(output)
        with patch("workspace.subprocess.run") as network:
            workspace.setup(output)
        network.assert_not_called()

    def test_changed_missing_or_unexpected_source_fails_verification(self) -> None:
        output = self.exported()
        source = output / "dependencies/zmk/source.c"
        source.write_text("modified")
        with self.assertRaisesRegex(ValueError, "integrity"):
            workspace.verify(output)
        source.unlink()
        with self.assertRaisesRegex(ValueError, "integrity"):
            workspace.verify(output)
        source.write_text("int source;\n")
        (source.parent / "extra.c").write_text("extra")
        with self.assertRaisesRegex(ValueError, "integrity"):
            workspace.verify(output)

    def test_links_cannot_escape_dependency(self) -> None:
        self.link.unlink()
        self.link.symlink_to(self.root / "dependencies/sources.lock.json")
        original = self.archive.read_bytes()
        with (
            patch.object(workspace, "verify"),
            self.assertRaisesRegex(ValueError, "escapes"),
        ):
            workspace.export(self.root, self.archive)
        self.assertEqual(self.archive.read_bytes(), original)

    def test_changed_pin_or_mixed_git_identity_is_rejected(self) -> None:
        output = self.exported()
        path = output / "dependencies/sources.lock.json"
        original = path.read_text()
        path.write_text(original.replace("a" * 40, "d" * 40))
        with self.assertRaisesRegex(ValueError, "integrity"):
            workspace.verify(output)
        path.write_text(original)
        (output / "dependencies/zmk/.git").mkdir()
        with self.assertRaisesRegex(ValueError, "mix Git"):
            workspace.verify(output)

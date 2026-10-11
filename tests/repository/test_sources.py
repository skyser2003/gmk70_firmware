import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "project_build", PROJECT / "tools/build-support/project_build.py"
)
assert SPEC is not None and SPEC.loader is not None
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class DependencyTests(unittest.TestCase):
    def test_checkout_matches_release_and_library_pins(self) -> None:
        BUILD.verify(PROJECT)
        locked = BUILD.pin(PROJECT)
        self.assertEqual(
            locked["repository"], "https://github.com/qmk/qmk_firmware.git"
        )
        self.assertRegex(locked["tag"], r"^\d+\.\d+\.\d+$")
        self.assertIn("path = dependencies/qmk", (PROJECT / ".gitmodules").read_text())

    def test_wrong_revision_is_rejected(self) -> None:
        if not (PROJECT / "dependencies/qmk/.git").exists():
            self.skipTest("Source archive has no Git metadata")
        with (
            patch.object(BUILD, "git", return_value="0" * 40),
            self.assertRaisesRegex(ValueError, "does not match"),
        ):
            BUILD.verify(PROJECT)

    def test_dirty_dependency_is_rejected(self) -> None:
        if not (PROJECT / "dependencies/qmk/.git").exists():
            self.skipTest("Source archive has no Git metadata")
        revision = BUILD.pin(PROJECT)["commit"]
        with (
            patch.object(BUILD, "git", side_effect=[revision, " M quantum/quantum.c"]),
            self.assertRaisesRegex(ValueError, "local changes"),
        ):
            BUILD.verify(PROJECT)


class WorkspaceTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.write("dependencies/qmk/quantum/quantum.c", "upstream")
        self.write("dependencies/qmk/lib/python/qmk/cli/__init__.py", "CLI")
        self.write("dependencies/qmk/lib/chibios/license.txt", "license")
        self.write(
            "dependencies/qmk/.gitmodules",
            '[submodule "lib/chibios"]\npath = lib/chibios\n',
        )
        self.write(
            "dependencies/sources.lock.json",
            json.dumps({"qmk": {"libraries": {"lib/chibios": "a" * 40}}}),
        )
        self.write("firmware/qmk-common/keyboards/zuoya/gmk70/gmk70.c", "board")
        self.write("firmware/qmk-common/drivers/wireless/wireless.c", "radio")
        self.write("firmware/qmk-via/keymaps/release/keymap.c", "keymap")
        self.write("firmware/qmk-via/keymaps/debug/keymap.c", "diagnostic")

    def write(self, name: str, content: str) -> Path:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def test_prepare_uses_sources_in_place_and_preserves_objects(self) -> None:
        with patch.object(BUILD, "verify"):
            debug = BUILD.prepare(self.root, "debug")
            release = BUILD.prepare(self.root, "release")
        self.assertNotEqual(debug, release)
        for directory in (debug, release):
            self.assertEqual(
                [file.name for file in directory.iterdir()],
                ["gmk70-source-origins.json"],
            )
            self.assertFalse(any(file.is_symlink() for file in directory.rglob("*")))
        origins_file = release / "gmk70-source-origins.json"
        origins = json.loads(origins_file.read_text())
        self.assertEqual(
            origins["drivers/wireless/wireless.c"],
            "firmware/qmk-common/drivers/wireless/wireless.c",
        )
        obj = release / ".build/kept.o"
        obj.parent.mkdir()
        obj.write_bytes(b"compiled object")
        timestamp = origins_file.stat().st_mtime_ns
        with patch.object(BUILD, "verify"):
            self.assertEqual(BUILD.prepare(self.root, "release"), release)
        self.assertEqual(obj.read_bytes(), b"compiled object")
        self.assertEqual(origins_file.stat().st_mtime_ns, timestamp)

    def test_normal_and_updater_caches_are_isolated(self) -> None:
        with patch.object(BUILD, "verify"):
            normal = BUILD.prepare(self.root, "debug")
            updater = BUILD.prepare(self.root, "debug", mode="updater")
        self.assertNotEqual(normal, updater)
        self.assertEqual(normal.parent, updater.parent)
        self.assertEqual(normal.name, "normal")
        self.assertEqual(updater.name, "updater")
        self.write("artifacts/qmk/release/normal.bin", "existing firmware")
        BUILD.clean(self.root)
        self.assertFalse(normal.exists())
        self.assertFalse(updater.exists())
        self.assertEqual(
            (self.root / "artifacts/qmk/release/normal.bin").read_text(),
            "existing firmware",
        )

    def test_source_collisions_are_rejected(self) -> None:
        self.write("firmware/qmk-common/quantum/quantum.c", "overwrite")
        with (
            patch.object(BUILD, "verify"),
            self.assertRaisesRegex(ValueError, "overwrite"),
        ):
            BUILD.prepare(self.root, "default")

    def test_platform_source_collisions_are_rejected(self) -> None:
        self.write("firmware/qmk-via/compat/gmk70.c", "overwrite")
        with (
            patch.object(BUILD, "verify"),
            self.assertRaisesRegex(ValueError, "overwrite"),
        ):
            BUILD.prepare(self.root, "release")

    def test_prepare_refuses_redirected_outputs_and_source_symlinks(self) -> None:
        redirected = self.root / ".work/qmk/release/normal"
        redirected.parent.mkdir(parents=True)
        redirected.symlink_to(self.root / "firmware", target_is_directory=True)
        with (
            patch.object(BUILD, "verify"),
            self.assertRaisesRegex(ValueError, "Refusing"),
        ):
            BUILD.prepare(self.root, "release")
        redirected.unlink()
        source = self.root / "firmware/qmk-via/compat/linked.c"
        source.parent.mkdir(parents=True)
        source.symlink_to(self.root / "dependencies/qmk/quantum/quantum.c")
        with (
            patch.object(BUILD, "verify"),
            self.assertRaisesRegex(ValueError, "Source symlinks"),
        ):
            BUILD.prepare(self.root, "release")

    def test_prepare_rejects_unknown_modes(self) -> None:
        with self.assertRaisesRegex(ValueError, "Unsupported build mode"):
            BUILD.prepare(self.root, "release", mode="../escape")

    def test_unselected_nested_libraries_are_not_exported(self) -> None:
        self.write(
            "dependencies/qmk/lib/chibios/.gitmodules",
            '[submodule "optional-sdk"]\npath = optional-sdk\n',
        )
        self.write("dependencies/qmk/lib/chibios/optional-sdk/unused.a", "unused")
        self.assertNotIn(
            "qmk/lib/chibios/optional-sdk/unused.a", BUILD.dependency_hashes(self.root)
        )

    def test_clean_preserves_sources_dependencies_and_artifacts(self) -> None:
        generated = self.write(".work/qmk/default/.build/firmware.elf", "temporary")
        legacy = self.write(".work/qmk/via_diagnostic/.build/firmware.elf", "legacy")
        compiler = self.write(".work/toolchains/compiler", "tool")
        artifact = self.write("artifacts/qmk/default/firmware.bin", "published")
        BUILD.clean(self.root)
        BUILD.clean(self.root)
        self.assertFalse(generated.exists())
        self.assertFalse(legacy.exists())
        self.assertEqual(compiler.read_text(), "tool")
        self.assertEqual(artifact.read_text(), "published")
        self.assertEqual(
            (self.root / "firmware/qmk-common/drivers/wireless/wireless.c").read_text(),
            "radio",
        )
        self.assertEqual(
            (self.root / "dependencies/qmk/quantum/quantum.c").read_text(), "upstream"
        )

    def test_clean_refuses_redirected_workspace(self) -> None:
        for name in (".work", ".work/qmk", ".work/qmk/default"):
            with self.subTest(path=name):
                redirect = self.root / name
                redirect.parent.mkdir(parents=True, exist_ok=True)
                redirect.symlink_to(self.root / "firmware", target_is_directory=True)
                with self.assertRaisesRegex(ValueError, "Refusing"):
                    BUILD.clean(self.root)
                redirect.unlink()
        self.assertTrue(
            (self.root / "firmware/qmk-common/drivers/wireless/wireless.c").is_file()
        )

    def test_archive_integrity_rejects_missing_changed_and_extra_files(self) -> None:
        manifest = self.root / "dependencies/source-files.json"
        manifest.write_text(json.dumps(BUILD.dependency_hashes(self.root)))
        BUILD.verify(self.root)
        original = self.root / "dependencies/qmk/quantum/quantum.c"
        original.write_text("modified")
        with self.assertRaisesRegex(ValueError, "integrity"):
            BUILD.verify(self.root)
        original.write_text("upstream")
        extra = self.write("dependencies/qmk/extra.c", "extra")
        with self.assertRaisesRegex(ValueError, "integrity"):
            BUILD.verify(self.root)
        extra.unlink()
        original.unlink()
        with self.assertRaisesRegex(ValueError, "sources are missing"):
            BUILD.verify(self.root)


if __name__ == "__main__":
    unittest.main()

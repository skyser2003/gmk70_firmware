"""Pinning, overlay boundaries and embedded Vial contract regressions."""

import importlib.util
import json
import re
import tarfile
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


class VialTests(unittest.TestCase):
    def test_radio_diagnostic_build_has_separate_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch.object(BUILD, "verify"):
                try:
                    diagnostic = BUILD.prepare_vial(root, "radio-diagnostic-updater")
                except ValueError as error:
                    self.fail(f"Optional radio diagnostic build is missing: {error}")
                normal = BUILD.prepare_vial(root, "normal")
                updater = BUILD.prepare_vial(root, "updater")
            self.assertEqual(len({diagnostic, normal, updater}), 3)
            self.assertTrue((diagnostic / "gmk70-source-origins.json").is_file())

    def test_independent_pinned_dependency(self) -> None:
        pin = BUILD.pin(PROJECT, "vial")
        self.assertEqual(pin["repository"], "https://github.com/vial-kb/vial-qmk.git")
        self.assertRegex(pin["commit"], r"^[0-9a-f]{40}$")
        self.assertNotEqual(pin["commit"], BUILD.pin(PROJECT)["commit"])
        if (PROJECT / "dependencies/vial-qmk/quantum/quantum.c").exists():
            BUILD.verify(PROJECT, "vial")

    def test_vial_pin_does_not_relax_official_qmk_release_requirement(self) -> None:
        if not (PROJECT / "dependencies/qmk/.git").exists():
            self.skipTest("Exported sources have no Git metadata")
        locked = dict(BUILD.pin(PROJECT))
        locked["tag"] = ""
        with (
            patch.object(BUILD, "pin", return_value=locked),
            self.assertRaisesRegex(ValueError, "requires a pinned release"),
        ):
            BUILD.verify(PROJECT)

    def test_physical_unlock_and_definition(self) -> None:
        keymap = PROJECT / "firmware/qmk-vial/keymaps/vial"
        stock = json.loads((PROJECT / "definitions/via/ZUOYA_GMK70.json").read_text())
        definition = json.loads((keymap / "vial.json").read_text())
        stock_keys = [
            item.split("\n")[0]
            for row in stock["layouts"]["keymap"]
            for item in row
            if isinstance(item, str)
        ]
        vial_keys = [
            item.split("\n")[0]
            for row in definition["layouts"]["keymap"]
            for item in row
            if isinstance(item, str) and not item.endswith("\ne")
        ]
        self.assertEqual(vial_keys, stock_keys)
        self.assertEqual(definition["matrix"], stock["matrix"])
        config = (keymap / "config.h").read_text()
        self.assertIn("VIAL_UNLOCK_COMBO_ROWS {0, 3}", config)
        self.assertIn("VIAL_UNLOCK_COMBO_COLS {1, 1}", config)
        uid = re.search(r"VIAL_KEYBOARD_UID\s*\{([^}]+)\}", config)
        assert uid is not None
        self.assertEqual(len(re.findall(r"0x[0-9A-F]{2}", uid[1])), 8)
        self.assertNotIn("VIAL_INSECURE", config)
        self.assertIn("DYNAMIC_KEYMAP_EEPROM_MAX_ADDR 0xFFB", config)
        self.assertEqual(len(definition["customKeycodes"]), 0x22)
        self.assertIn("0x7E19", definition["customKeycodes"][0x19]["title"])
        self.assertEqual(definition["customKeycodes"][7]["shortName"], "Show Charging")
        self.assertEqual(definition["customKeycodes"][9]["shortName"], "Logo Effects")
        self.assertEqual(definition["customKeycodes"][26]["shortName"], "Num/F Toggle")
        self.assertEqual(definition["customKeycodes"][27]["shortName"], "Show Battery")

    def test_encoder_rotation_controls_and_press(self) -> None:
        keymap = PROJECT / "firmware/qmk-vial/keymaps/vial"
        definition = json.loads((keymap / "vial.json").read_text())
        labels = [
            item.split("\n")
            for row in definition["layouts"]["keymap"]
            for item in row
            if isinstance(item, str)
        ]
        # KLE alignment 4 puts serial label 9 in the center; Vial recognizes
        # an exact "e" there, with encoder index/direction in label 0.
        encoders = [label for label in labels if len(label) > 9 and label[9] == "e"]
        self.assertCountEqual([label[0] for label in encoders], ["0,0", "0,1"])
        matrix = [label[0] for label in labels if label not in encoders]
        self.assertEqual(matrix.count("0,0"), 1, "Keep the knob's physical press")
        self.assertIn("ENCODER_MAP_ENABLE = yes", (keymap / "rules.mk").read_text())

    def test_preparation_shares_board_and_excludes_stock_parser(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            files = {
                "dependencies/sources.lock.json": json.dumps(
                    {"vial": {"libraries": {}}}
                ),
                "dependencies/vial-qmk/quantum/vial.c": "fork",
                "firmware/qmk-common/keyboards/zuoya/gmk70/gmk70.c": "board",
                "firmware/qmk-common/keyboards/zuoya/gmk70/keymaps/stock_keymap.h": "map",
                "firmware/qmk-common/drivers/wireless/lowpower.c": "power",
                "firmware/qmk-via/compat/stock_via.c": "VIA only",
                "firmware/qmk-vial/compat/vial_adapter.c": "Vial only",
                "firmware/qmk-vial/keymaps/vial/keymap.c": "shared map include",
            }
            for name, content in files.items():
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(content)
            with patch.object(BUILD, "verify"):
                destination = BUILD.prepare_vial(root)
            origins = json.loads(
                (destination / "gmk70-source-origins.json").read_text()
            )
            self.assertEqual(
                origins["drivers/wireless/lowpower.c"],
                "firmware/qmk-common/drivers/wireless/lowpower.c",
            )
            self.assertIn("keyboards/zuoya/gmk70/vial_adapter.c", origins)
            self.assertNotIn("keyboards/zuoya/gmk70/stock_via.c", origins)
            self.assertFalse((destination / "keyboards").exists())
            self.assertFalse((destination / "quantum").exists())
            self.assertFalse(any(file.is_symlink() for file in destination.rglob("*")))
            BUILD.clean(root)
            self.assertFalse(destination.exists())
            self.assertTrue((root / "dependencies/vial-qmk/quantum/vial.c").is_file())

    def test_vial_export_is_self_contained_and_excludes_official_qmk(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "project"
            directories = (
                "firmware",
                "definitions",
                "scripts",
                "tests",
                "tools",
                "docs",
                ".github",
            )
            files = {
                "dependencies/sources.lock.json": json.dumps(
                    {"vial": {"libraries": {}}}
                ),
                "dependencies/vial-qmk/quantum/quantum.c": "selected framework",
                "dependencies/qmk/quantum/quantum.c": "other framework",
            }
            for name in directories:
                files[f"{name}/source.txt"] = name
            for name in (
                "Makefile",
                "README.md",
                "LICENSE",
                "AGENTS.md",
                ".gitattributes",
                ".gitignore",
                ".gitmodules",
                ".clang-format",
            ):
                files[name] = name
            for name, content in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            (root / "dependencies/source-files.json").write_text(
                json.dumps(BUILD.dependency_hashes(root, "vial"))
            )
            output = Path(temporary) / "vial.tar.gz"
            BUILD.export_sources(root, output, "vial")
            exported = Path(temporary) / "exported"
            with tarfile.open(output) as archive:
                archive.extractall(exported, filter="data")
            self.assertFalse((exported / "dependencies/qmk").exists())
            self.assertEqual(
                (exported / "dependencies/vial-qmk/quantum/quantum.c").read_text(),
                "selected framework",
            )
            BUILD.verify(exported, "vial")
            (exported / "dependencies/vial-qmk/quantum/quantum.c").write_text("changed")
            with self.assertRaisesRegex(ValueError, "integrity check"):
                BUILD.verify(exported, "vial")


if __name__ == "__main__":
    unittest.main()

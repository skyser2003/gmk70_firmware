import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from keymap_migration import JsonValue, LegacyKeymapMigration


class MigrationTests(unittest.TestCase):
    def test_via_export_keeps_metadata_and_macros(self) -> None:
        original: dict[str, JsonValue] = {
            "name": "GMK70",
            "layers": [["0x7E03", "USER05", "QK_KB_8", "KC_A", 0x7E07]],
            "encoders": [[[0x7E04, 0x7E06]]],
            "macros": [["0x7E05"]],
        }
        converted = LegacyKeymapMigration.convert(original)
        self.assertEqual(
            converted["layers"], [["0x7E05", "USER27", "QK_KB_28", "KC_A", 0x7E07]]
        )
        self.assertEqual(converted["encoders"], [[[0x7E09, 0x7E1A]]])
        self.assertEqual(converted["macros"], original["macros"])
        self.assertEqual(
            original["layers"], [["0x7E03", "USER05", "QK_KB_8", "KC_A", 0x7E07]]
        )

    def test_all_legacy_slots(self) -> None:
        expected = [
            0x7E00,
            0x7E01,
            0x7E02,
            0x7E05,
            0x7E09,
            0x7E1B,
            0x7E1A,
            0x7E07,
            0x7E1C,
            0x7E1D,
            0x7E1E,
            0x7E1F,
            0,
        ]
        converted = LegacyKeymapMigration.convert(
            {"layers": [list(range(0x7E00, 0x7E0D))]}
        )
        self.assertEqual(converted["layers"], [expected])

    def test_raw_backup_and_repeat_rejection(self) -> None:
        converted = LegacyKeymapMigration.convert(
            {"matrix_keycodes": [[["0x7e05"]]], "planned_changes": []}
        )
        self.assertEqual(converted["matrix_keycodes"], [[["0x7E1B"]]])
        self.assertNotIn("planned_changes", converted)
        with self.assertRaises(ValueError):
            LegacyKeymapMigration.convert(converted)

    def test_invalid_inputs(self) -> None:
        invalid_inputs: tuple[JsonValue, ...] = (
            {"layouts": {}},
            {"layers": "bad"},
            {"layers": [[True]]},
            {"layers": [[65536]]},
            {"layers": [[{}]]},
            {"layers": [["USER64"]]},
        )
        for invalid in invalid_inputs:
            with self.assertRaises((ValueError, TypeError)):
                LegacyKeymapMigration.convert(invalid)

    def test_no_in_place_or_output_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "input.json"
            destination = Path(folder) / "output.json"
            source.write_text('{"layers": [[32261]]}', encoding="utf-8")
            original = source.read_bytes()
            with self.assertRaises(ValueError):
                LegacyKeymapMigration.convert_file(source, source)
            LegacyKeymapMigration.convert_file(source, destination)
            self.assertEqual(json.loads(destination.read_text())["layers"], [[0x7E1B]])
            self.assertEqual(source.read_bytes(), original)
            with self.assertRaises(FileExistsError):
                LegacyKeymapMigration.convert_file(source, destination)


if __name__ == "__main__":
    unittest.main()

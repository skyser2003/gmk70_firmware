"""Generic offline Vial-to-Studio migration tests; no personal fixtures."""

import json
import stat
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from keymap_migration import JsonValue
from vial_to_zmk import (
    Binding,
    IssueCategory,
    IssueDisposition,
    UnsupportedKeycode,
    UnsupportedPolicy,
    VialToZmk,
)


class VialToZmkTests(unittest.TestCase):
    _root = Path(__file__).resolve().parents[3]

    def setUp(self) -> None:
        self.converter = VialToZmk(self._root)

    @staticmethod
    def _document() -> dict[str, JsonValue]:
        layers: list[JsonValue] = [
            [["KC_NO" for _ in range(9)] for _ in range(10)] for _ in range(4)
        ]
        return {
            "version": 1,
            "vial_protocol": 6,
            "via_protocol": 9,
            "layout": layers,
            "encoder_layout": [[], [], [], []],
            "macro": [[] for _ in range(16)],
            "layout_options": -1,
        }

    @staticmethod
    def _put(
        document: dict[str, JsonValue], row: int, column: int, keycode: JsonValue
    ) -> None:
        layout = document["layout"]
        assert isinstance(layout, list)
        layer = layout[0]
        assert isinstance(layer, list)
        cells = layer[row]
        assert isinstance(cells, list)
        cells[column] = keycode

    def test_ordinary_hid_aliases_chords_and_empty_bindings(self) -> None:
        cases: tuple[tuple[JsonValue, Binding], ...] = (
            ("KC_A", Binding("kp", (0x70004,))),
            (4, Binding("kp", (0x70004,))),
            ("0x0004", Binding("kp", (0x70004,))),
            ("KC_LSHIFT", Binding("kp", (0x700E1,))),
            ("KC_RCTRL", Binding("kp", (0x700E4,))),
            ("KC_AUDIO_MUTE", Binding("kp", (0xC00E2,))),
            ("LSFT(KC_A)", Binding("kp", (0x02070004,))),
            ("LCTL(LSFT(KC_A))", Binding("kp", (0x03070004,))),
            ("RALT(KC_A)", Binding("kp", (0x40070004,))),
            (0x1404, Binding("kp", (0x40070004,))),
            ("KC_EXLM", Binding("kp", (0x0207001E,))),
            ("KC_NO", Binding("none", ())),
            ("KC_TRNS", Binding("trans", ())),
        )
        for keycode, expected in cases:
            with self.subTest(keycode=keycode):
                self.assertEqual(self.converter.translate(keycode, 4), expected)

    def test_layers_rgb_and_battery_use_studio_parameters(self) -> None:
        cases: tuple[tuple[JsonValue, Binding], ...] = (
            ("MO(1)", Binding("mo", (1,))),
            (0x5223, Binding("mo", (3,))),
            ("TO(2)", Binding("to", (2,))),
            ("TG(3)", Binding("tog", (3,))),
            ("RGB_TOG", Binding("rgb_ug", (0, 0))),
            ("UG_HUEU", Binding("rgb_ug", (3, 0))),
            (0x7849, Binding("rgb_ug", (7, 0))),
            ("USER27", Binding("gmk_battery", (0,))),
            (0x7E07, Binding("gmk_battery", (1,))),
            ("GMK70_WINDOWS_MODE", Binding("to", (0,))),
            ("GMK70_MAC_MODE", Binding("to", (2,))),
            ("GMK70_RGB_TOGGLE", Binding("rgb_ug", (0, 0))),
        )
        for keycode, expected in cases:
            with self.subTest(keycode=keycode):
                self.assertEqual(self.converter.translate(keycode, 4), expected)

    def test_radio_profiles_preserve_stock_device_numbering(self) -> None:
        cases: tuple[tuple[JsonValue, int], ...] = (
            (0x7E00, 1),
            (0x7E01, 2),
            (0x7E02, 3),
            (0x7E03, 4),
            (0x7E04, 5),
            (0x7E05, 6),
            (0x7E06, 0),
            ("USER00", 1),
            ("USER05", 6),
            ("USER06", 0),
        )
        for keycode, parameter in cases:
            with self.subTest(keycode=keycode):
                self.assertEqual(
                    self.converter.translate(keycode, 4),
                    Binding("gmk_radio", (parameter,)),
                )

    def test_radio_conversion_is_ready_without_an_unassigned_disposition(self) -> None:
        document = self._document()
        for column, code in enumerate(range(0x7E00, 0x7E07)):
            self._put(document, 0, column, code)
        converted = self.converter.convert(document)
        self.assertTrue(converted.ready)
        self.assertEqual(converted.issues, ())
        self.assertEqual(
            converted.layers[0].bindings[:7],
            tuple(Binding("gmk_radio", (value,)) for value in (1, 2, 3, 4, 5, 6, 0)),
        )

    def test_board_geometry_orders_both_halves_and_source_is_not_mutated(self) -> None:
        document = self._document()
        self._put(document, 5, 1, "KC_A")
        self._put(document, 9, 8, "KC_B")
        original = json.dumps(document)
        converted = self.converter.convert(document)
        self.assertTrue(converted.ready)
        self.assertEqual(len(converted.layers), 4)
        self.assertEqual(len(converted.layers[0].bindings), 72)
        self.assertEqual(converted.layers[0].bindings[8], Binding("kp", (0x70004,)))
        self.assertEqual(converted.layers[0].bindings[71], Binding("kp", (0x70005,)))
        self.assertEqual(json.dumps(document), original)

    def test_unsupported_cells_and_active_auxiliary_features_block_readiness(
        self,
    ) -> None:
        document = self._document()
        self._put(document, 0, 0, "USER26")
        self._put(document, 5, 0, "KC_A")  # An unused matrix cell.
        self._put(document, 0, 1, "MO(4)")  # Missing destination layer.
        self._put(document, 0, 2, "KC_UNKNOWN_PERSONAL_TOKEN")
        document["macro"] = [["secret macro payload"]]
        document["encoder_layout"] = [[["KC_A", "KC_B"]], [], [], []]
        converted = self.converter.convert(document)
        self.assertFalse(converted.ready)
        self.assertIsNone(converted.layers[0].bindings[0])
        counts = converted.issue_counts()
        self.assertEqual(counts["function-flip"], 1)
        self.assertEqual(counts["unmapped-matrix-cell"], 1)
        self.assertEqual(counts["layer-out-of-range"], 1)
        self.assertEqual(counts["unknown-keycode"], 1)
        self.assertEqual(counts["macros"], 1)
        self.assertEqual(counts["encoders"], 2)
        self.assertNotIn("secret", converted.summary())
        self.assertNotIn("PERSONAL", converted.summary())

    def test_vial_minus_one_sentinel_is_only_valid_in_unused_matrix_cells(self) -> None:
        document = self._document()
        self._put(document, 5, 0, -1)
        self.assertTrue(self.converter.convert(document).ready)
        self._put(document, 0, 0, -1)
        with self.assertRaises(ValueError):
            self.converter.convert(document)

    def test_explicit_unassigned_policy_keeps_each_unsupported_key_disposition(
        self,
    ) -> None:
        document = self._document()
        self._put(document, 0, 0, "USER26")
        converted = self.converter.convert(document, UnsupportedPolicy.UNASSIGNED)
        self.assertTrue(converted.ready)
        self.assertEqual(converted.layers[0].bindings[0], Binding("none", (0, 0)))
        self.assertEqual(converted.issue_counts(), {"function-flip": 1})
        self.assertEqual(converted.issues[0].disposition, IssueDisposition.UNASSIGNED)
        self.assertIn("287 translated bindings", converted.summary())
        self.assertIn("1 explicitly unassigned", converted.summary())
        saved = converted.document("generic source hash")
        self.assertEqual(saved["unsupported_policy"], "unassigned")
        self.assertEqual(saved["ready_for_behavior_id_resolution"], True)

    def test_unassigned_policy_does_not_implicitly_drop_nonbinding_data(self) -> None:
        document = self._document()
        self._put(document, 0, 0, "USER26")
        document["macro"] = [["generic macro"]]
        converted = self.converter.convert(document, UnsupportedPolicy.UNASSIGNED)
        self.assertFalse(converted.ready)
        self.assertEqual(converted.issues[0].disposition, IssueDisposition.UNRESOLVED)

    def test_settings_reset_and_unimplemented_controls_are_reported_by_category(
        self,
    ) -> None:
        cases: tuple[tuple[JsonValue, IssueCategory], ...] = (
            ("QK_CLEAR_EEPROM", IssueCategory.SETTINGS_RESET),
            ("GU_TOGG", IssueCategory.GUI_LOCK),
            ("NK_TOGG", IssueCategory.NKRO),
            ("STOCK_USB_SLEEP_TOGGLE", IssueCategory.SLEEP),
            ("KC_FILP", IssueCategory.FUNCTION_FLIP),
            ("STOCK_RESERVED_19", IssueCategory.RESERVED),
            ("DF(2)", IssueCategory.LAYER_SEMANTICS),
        )
        for keycode, category in cases:
            with self.subTest(keycode=keycode):
                with self.assertRaises(UnsupportedKeycode) as raised:
                    self.converter.translate(keycode, 4)
                self.assertEqual(raised.exception.category, category)

    def test_active_auxiliary_rules_are_never_silently_discarded(self) -> None:
        document = self._document()
        document["tap_dance"] = [["KC_A", "KC_NO", "KC_NO", "KC_NO", 200]]
        document["combo"] = [["KC_A", "KC_B", "KC_NO", "KC_NO", "KC_C"]]
        document["key_override"] = [{"trigger": "KC_A", "replacement": "KC_B"}]
        document["alt_repeat_key"] = [{"keycode": "KC_A", "alt_keycode": "KC_B"}]
        document["settings"] = {"1": 200, "2": True}
        document["future_table"] = []
        converted = self.converter.convert(document)
        self.assertFalse(converted.ready)
        self.assertEqual(
            converted.issue_counts(),
            {
                "alternate-repeat": 1,
                "combos": 1,
                "key-overrides": 1,
                "tap-dance": 1,
                "unknown-export-field": 1,
            },
        )
        self.assertTrue(
            any(
                entry.category == "vial-tuning-settings-not-imported"
                and entry.count == 2
                for entry in converted.excluded
            )
        )

    def test_invalid_keycode_types_are_rejected_without_token_in_error(self) -> None:
        invalid: tuple[JsonValue, ...] = (True, None, {}, [], 65536, -1)
        for keycode in invalid:
            with (
                self.subTest(keycode=keycode),
                self.assertRaises((ValueError, TypeError)),
            ):
                self.converter.translate(keycode, 4)

    def test_versions_types_and_matrix_dimensions_are_validated(self) -> None:
        invalid_fields: tuple[tuple[str, JsonValue], ...] = (
            ("version", 2),
            ("vial_protocol", 5),
            ("via_protocol", True),
            ("layout", [[[]]]),
            ("layout_options", "0"),
        )
        for field, value in invalid_fields:
            document = self._document()
            document[field] = value
            with self.subTest(field=field), self.assertRaises((ValueError, TypeError)):
                self.converter.convert(document)

    def test_file_conversion_is_private_exclusive_and_preserves_source(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / "source.vil"
            destination = folder / "studio.json"
            source.write_text(json.dumps(self._document()), encoding="utf-8")
            original = source.read_bytes()
            result = self.converter.convert_file(source, destination)
            self.assertTrue(result.ready)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(stat.S_IMODE(destination.stat().st_mode), 0o600)
            saved: JsonValue = json.loads(destination.read_text(encoding="utf-8"))
            assert isinstance(saved, dict)
            self.assertEqual(saved["format"], "gmk70-zmk-studio-symbolic-v1")
            self.assertEqual(saved["layout_options"], -1)
            self.assertIn("source_sha256", saved)
            with self.assertRaises(FileExistsError):
                self.converter.convert_file(source, destination)
            with self.assertRaises(ValueError):
                self.converter.convert_file(source, source)


if __name__ == "__main__":
    unittest.main()

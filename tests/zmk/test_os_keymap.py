"""The native map must preserve stock Windows/Mac modifier and mode semantics."""

import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class OsKeymapTests(unittest.TestCase):
    def test_stock_lighting_controls_on_both_os_fn_layers(self) -> None:
        native = (
            ROOT / "firmware/zmk/boards/zuoya/gmk70_wired/gmk70_wired.keymap"
        ).read_text()
        layers = re.findall(r"bindings\s*=\s*<(.*?)>;", native, re.S)
        expected = {
            17: "RGB_EFF",
            28: "RGB_HUD",
            29: "RGB_HUI",
            30: "RGB_EFR",
            42: "RGB_SAD",
            43: "RGB_SAI",
            48: "RGB_TOG",
            59: "RGB_BRI",
            69: "RGB_SPD",
            70: "RGB_BRD",
            71: "RGB_SPI",
        }
        for layer in (layers[1], layers[3]):
            bindings = [b.strip() for b in re.findall(r"&\w+(?:\s+[^&\n]+)?", layer)]
            for position, command in expected.items():
                self.assertEqual(bindings[position], f"&rgb_ug {command}")

    def test_stock_os_layers_and_mode_switches(self) -> None:
        stock = (
            ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70/keymaps/stock_keymap.h"
        ).read_text()
        native = (
            ROOT / "firmware/zmk/boards/zuoya/gmk70_wired/gmk70_wired.keymap"
        ).read_text()
        native_layers = re.findall(r"bindings\s*=\s*<(.*?)>;", native, re.S)
        self.assertEqual(len(native_layers), 4)
        layers = [re.findall(r"&\w+(?:\s+[^&\n]+)?", layer) for layer in native_layers]
        layers = [[b.strip() for b in layer] for layer in layers]
        for layer in layers:
            self.assertEqual(len(layer), 72)
        # The stock base maps differ only at Fn and the three Alt/GUI positions.
        self.assertIn("MO(_MFL), KC_LGUI", stock)
        self.assertEqual(
            [(i, a, b) for i, (a, b) in enumerate(zip(layers[0], layers[2])) if a != b],
            [
                (63, "&mo 1", "&mo 3"),
                (64, "&kp LALT", "&kp LGUI"),
                (67, "&kp RALT", "&kp RGUI"),
                (68, "&kp RGUI", "&kp RALT"),
            ],
        )
        self.assertEqual(layers[1][34], "&to 2")  # Stock Fn+S -> Mac
        self.assertEqual(layers[3][33], "&to 0")  # Stock Fn+A -> Windows
        self.assertEqual(layers[3][64], "&kp LGUI")
        self.assertEqual(layers[3][67:69], ["&kp RGUI", "&kp RALT"])


if __name__ == "__main__":
    unittest.main()

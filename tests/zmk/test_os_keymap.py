"""The native map must preserve stock Windows/Mac modifier and mode semantics."""

from pathlib import Path
import unittest

from keymap_fixture import read_layers

ROOT = Path(__file__).resolve().parents[2]


class OsKeymapTests(unittest.TestCase):
    def test_stock_lighting_controls_on_both_os_fn_layers(self) -> None:
        layers = read_layers()
        expected = {
            17: "RGB_EFF",
            28: "RGB_HUD",
            29: "RGB_HUI",
            42: "RGB_SAD",
            43: "RGB_SAI",
            48: "RGB_TOG",
            59: "RGB_BRI",
            69: "RGB_SPD",
            70: "RGB_BRD",
            71: "RGB_SPI",
        }
        for layer in (layers[1], layers[3]):
            bindings = layer
            for position, command in expected.items():
                self.assertEqual(bindings[position], f"&rgb_ug {command}")

    def test_stock_os_layers_and_mode_switches(self) -> None:
        layers = read_layers()
        self.assertEqual(len(layers), 4)
        for layer in layers:
            self.assertEqual(len(layer), 74)
        self.assertEqual(layers[0][-2:], ["&kp C_VOL_DN", "&kp C_VOL_UP"])
        self.assertEqual(layers[2][-2:], layers[0][-2:])
        self.assertEqual(layers[1][-2:], ["&trans", "&trans"])
        self.assertEqual(layers[3][-2:], ["&trans", "&trans"])
        # The stock base maps differ only at Fn and the three Alt/GUI positions.
        self.assertEqual(
            [(i, a, b) for i, (a, b) in enumerate(zip(layers[0], layers[2])) if a != b],
            [
                (63, "&mo 1", "&mo 3"),
                (64, "&kp LALT", "&kp LGUI"),
                (67, "&kp RALT", "&kp RGUI"),
                (68, "&kp RGUI", "&kp RALT"),
            ],
        )
        self.assertEqual(
            layers[1][34], "&gmk_controls GMK70_OS_MAC"
        )  # Stock Fn+S -> Mac
        self.assertEqual(
            layers[3][33], "&gmk_controls GMK70_OS_WINDOWS"
        )  # Stock Fn+A -> Windows
        self.assertEqual(layers[3][64], "&kp LGUI")
        self.assertEqual(layers[3][67:69], ["&kp RGUI", "&kp RALT"])


if __name__ == "__main__":
    unittest.main()

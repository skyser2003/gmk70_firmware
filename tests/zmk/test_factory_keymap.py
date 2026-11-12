"""Compare each physical ZMK factory binding with the authoritative QMK map."""

import re
import unittest

from keymap_fixture import ROOT, read_layers


class FactoryKeymapTests(unittest.TestCase):
    def test_all_stock_positions_and_board_actions(self) -> None:
        source = (
            ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70/keymaps/stock_keymap.h"
        ).read_text()
        source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
        expected_layers = []
        for match in re.finditer(r"\[_(?:BL|FL|MBL|MFL)\]\s*=\s*LAYOUT\(", source):
            cursor, start, depth = match.end(), match.end(), 1
            while depth:
                depth += (source[cursor] == "(") - (source[cursor] == ")")
                cursor += 1
            expected_layers.append(
                [key.strip() for key in source[start : cursor - 1].split(",")]
            )
        actions = {
            "_______": "&trans",
            "KC_NO": "&none",
            "STOCK_RESERVED_19": "&none",
            # Separate decorative/logo effects have no ZMK implementation.
            "RL_MOD": "&none",
            "EE_CLR": "&gmk_controls GMK70_RECOVERY_SETTINGS_RESET",
            "KC_BT1": "&gmk_radio 1",
            "KC_BT2": "&gmk_radio 2",
            "KC_BT3": "&gmk_radio 3",
            "KC_2G4": "&gmk_radio 6",
            "HS_BATQ": "&gmk_battery 0",
            "KC_BATQ": "&gmk_battery 1",
            "KC_FILP": "&gmk_controls GMK70_FUNCTION_FLIP",
            "GU_TOGG": "&gmk_controls GMK70_GUI_TOGGLE",
            "NK_TOGG": "&gmk_controls GMK70_NKRO_TOGGLE",
            "TO(_BL)": "&gmk_controls GMK70_OS_WINDOWS",
            "TO(_MBL)": "&gmk_controls GMK70_OS_MAC",
            "MO(_FL)": "&mo 1",
            "MO(_MFL)": "&mo 3",
        }
        actions.update(
            {f"MOR_{i}": f"&kp LC({key})" for i, key in enumerate("ZXCV", 1)}
        )
        actions.update(
            {
                "UG_" + key: "&rgb_ug " + value
                for key, value in {
                    "NEXT": "RGB_EFF",
                    "TOGG": "RGB_TOG",
                    "HUED": "RGB_HUD",
                    "HUEU": "RGB_HUI",
                    "SATD": "RGB_SAD",
                    "SATU": "RGB_SAI",
                    "VALU": "RGB_BRI",
                    "VALD": "RGB_BRD",
                    "SPDD": "RGB_SPD",
                    "SPDU": "RGB_SPI",
                }.items()
            }
        )
        aliases = {
            "MUTE": "C_MUTE",
            "GRV": "GRAVE",
            "MINS": "MINUS",
            "EQL": "EQUAL",
            "BSLS": "BSLH",
            "LBRC": "LBKT",
            "RBRC": "RBKT",
            "SCLN": "SEMI",
            "QUOT": "SQT",
            "ENT": "RET",
            "LSFT": "LSHFT",
            "RSFT": "RSHFT",
            "COMM": "COMMA",
            "SLSH": "FSLH",
            "LCTL": "LCTRL",
            "SPC": "SPACE",
            "RGHT": "RIGHT",
            "PSCR": "PSCRN",
            "SCRL": "SLCK",
            "PAUSE": "PAUSE_BREAK",
            "PGUP": "PG_UP",
            "PGDN": "PG_DN",
        }
        actual = read_layers()
        self.assertEqual(len(expected_layers), 4)
        self.assertEqual([len(layer) for layer in actual], [74] * 4)
        for layer, expected in enumerate(expected_layers):
            self.assertEqual(len(expected), 72)
            for position, key in enumerate(expected):
                if key in actions:
                    binding = actions[key]
                else:
                    self.assertTrue(key.startswith("KC_"), key)
                    usage = key[3:]
                    binding = "&kp " + (
                        "N" + usage if usage.isdigit() else aliases.get(usage, usage)
                    )
                self.assertEqual(
                    actual[layer][position], binding, (layer, position, key)
                )
        self.assertEqual(actual[0][-2:], ["&kp C_VOL_DN", "&kp C_VOL_UP"])
        self.assertEqual(actual[2][-2:], actual[0][-2:])
        self.assertEqual(actual[1][-2:], ["&trans", "&trans"])
        self.assertEqual(actual[3][-2:], actual[1][-2:])

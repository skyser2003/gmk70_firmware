# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare maintained defaults with tables extracted from the supplied V15 HEX."""

import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70"


class StockTablesTests(unittest.TestCase):
    def test_default_keymaps_and_encoders_are_stock_bytes(self) -> None:
        info = json.loads((BOARD / "keyboard.json").read_text())
        stock = json.loads((ROOT / "tests/fixtures/stock-v15-tables.json").read_text())
        positions = info["layouts"]["LAYOUT"]["layout"]
        left_encoders = info["encoder"]["rotary"]
        right_encoders = (
            info["split"]
            .get("encoder", {})
            .get("right", {})
            .get("rotary", left_encoders)
        )
        matrix = [["KC_NO"] * 9 for _ in range(10)]
        for i, position in enumerate(positions):
            row, col = position["matrix"]
            matrix[row][col] = f"k{i}"
        initializer = "{" + ",".join("{" + ",".join(row) + "}" for row in matrix) + "}"
        source = "\n".join(
            [
                "#include <stdio.h>",
                "#include <stdint.h>",
                '#include "quantum_keycodes.h"',
                '#include "stock_keycodes.h"',
                '#include "config.h"',
                '#define QMK_KEYBOARD_H "quantum_keycodes.h"',
                "#define PROGMEM",
                "#define MATRIX_ROWS 10",
                "#define MATRIX_COLS 9",
                "#define ENCODER_ENABLE",
                "#define ENCODER_MAP_ENABLE",
                "#define SPLIT_KEYBOARD",
                "#define ENCODER_A_PINS {" + ",".join("0" for _ in left_encoders) + "}",
                "#define ENCODER_A_PINS_RIGHT {"
                + ",".join("0" for _ in right_encoders)
                + "}",
                '#include "encoder.h"',
                "#define LAYOUT("
                + ",".join(f"k{i}" for i in range(len(positions)))
                + ") "
                + initializer,
                '#include "keymaps/stock_keymap.h"',
                "int main(void) { fwrite(keymaps,sizeof(keymaps),1,stdout); fwrite(encoder_map,sizeof(encoder_map),1,stdout); }",
            ]
        )
        with tempfile.TemporaryDirectory() as tmp:
            c_file = Path(tmp) / "keymap.c"
            binary = Path(tmp) / "keymap"
            c_file.write_text(source)
            subprocess.run(
                [
                    "cc",
                    "-std=gnu11",
                    "-Wall",
                    "-Werror",
                    "-I" + str(BOARD),
                    "-I" + str(ROOT / "tests/qmk/stubs"),
                    "-I" + str(ROOT / "dependencies/qmk/quantum"),
                    "-I" + str(ROOT / "dependencies/qmk/quantum/keymap_extras"),
                    "-I" + str(ROOT / "dependencies/qmk/quantum/sequencer"),
                    str(c_file),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
            )
            data = subprocess.check_output([binary])
        values = list(struct.unpack("<368H", data))
        self.assertEqual(values[:360], stock["keymaps"])
        self.assertEqual(values[360:], stock["encoder"])

    def test_rgb_geometry_flags_and_physical_lookup_are_stock(self) -> None:
        info = json.loads((BOARD / "keyboard.json").read_text())
        stock = json.loads((ROOT / "tests/fixtures/stock-v15-tables.json").read_text())
        leds = info["rgb_matrix"]["layout"]
        self.assertEqual([[led["x"], led["y"]] for led in leds], stock["led_points"])
        self.assertEqual([led["flags"] for led in leds], stock["led_flags"])
        matrix = [255] * 90
        for index, led in enumerate(leds):
            if "matrix" in led:
                row, col = led["matrix"]
                matrix[row * 9 + col] = index
        # The native lighting test verifies the post-init values in unwired slots.
        for row in range(5):
            matrix[row * 9 + 8] = 0
        self.assertEqual(matrix, stock["led_matrix"])

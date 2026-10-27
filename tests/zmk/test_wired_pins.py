"""The native declarations must follow maintained board pin changes."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "wired_pins", ROOT / "firmware/zmk/tools/wired_pins.py"
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class WiredPinsTest(unittest.TestCase):
    def test_authoritative_matrix(self) -> None:
        header = MODULE.generate(
            ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json"
        )
        self.assertIn("gmk_left_cols[9]", header)
        self.assertIn("{255, 0}", header)
        self.assertIn("gmk_role_pin = {1, 9}", header)
        self.assertIn("gmk_right_rows[5]", header)

    def test_mutation_and_bounds(self) -> None:
        keyboard = json.loads(
            (
                ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json"
            ).read_text()
        )
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "keyboard.json"
            keyboard["matrix_pins"]["cols"][0] = "D15"
            source.write_text(json.dumps(keyboard))
            self.assertIn("{3, 15}", MODULE.generate(source))
            keyboard["matrix_pins"]["cols"][0] = "D16"
            source.write_text(json.dumps(keyboard))
            with self.assertRaises(ValueError):
                MODULE.generate(source)
            keyboard["matrix_pins"]["cols"] = []
            source.write_text(json.dumps(keyboard))
            with self.assertRaises(ValueError):
                MODULE.generate(source)


if __name__ == "__main__":
    unittest.main()

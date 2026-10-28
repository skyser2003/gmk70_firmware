"""Studio geometry must match VIA while preserving the firmware matrix order."""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
KEYBOARD = ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json"
VIA = ROOT / "definitions/via/ZUOYA_GMK70.json"
GENERATOR = ROOT / "firmware/zmk/tools/studio_layout.py"


class StudioLayoutTests(unittest.TestCase):
    def generate(
        self, keyboard: dict[str, Any], via: dict[str, Any]
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / "keyboard.json").write_text(json.dumps(keyboard))
            (base / "via.json").write_text(json.dumps(via))
            result = subprocess.run(
                [
                    sys.executable,
                    str(GENERATOR),
                    "--keyboard",
                    str(base / "keyboard.json"),
                    "--via",
                    str(base / "via.json"),
                    "--output",
                    str(base / "layout.overlay"),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode == 0:
                result.stdout = (base / "layout.overlay").read_text()
            return result

    def test_actual_geometry_in_matrix_order(self) -> None:
        result = self.generate(
            json.loads(KEYBOARD.read_text()), json.loads(VIA.read_text())
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        attrs = [
            tuple(map(int, row))
            for row in re.findall(
                r"<&key_physical_attrs\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+0\s+0\s+0>",
                result.stdout,
            )
        ]
        self.assertEqual(len(attrs), 72)
        self.assertEqual(
            attrs[:3], [(100, 100, 0, 0), (100, 100, 125, 0), (100, 100, 225, 0)]
        )
        self.assertEqual(attrs[16], (100, 100, 0, 100))
        self.assertEqual(attrs[33], (100, 100, 300, 200))
        self.assertEqual(attrs[63], (125, 100, 250, 400))
        self.assertEqual(attrs[66], (275, 100, 900, 400))

    def test_source_geometry_changes_and_matrix_reordering_are_reflected(self) -> None:
        keyboard = json.loads(KEYBOARD.read_text())
        via = json.loads(VIA.read_text())
        via["layouts"]["keymap"][0][0]["w"] = 1.5
        keyboard["layouts"]["LAYOUT"]["layout"][:2] = keyboard["layouts"]["LAYOUT"][
            "layout"
        ][1::-1]
        result = self.generate(keyboard, via)
        self.assertEqual(result.returncode, 0, result.stderr)
        attrs = re.findall(r"<&key_physical_attrs\s+([\d ]+)>", result.stdout)
        self.assertEqual(tuple(map(int, attrs[0].split())), (100, 100, 175, 0, 0, 0, 0))
        self.assertEqual(tuple(map(int, attrs[1].split())), (150, 100, 0, 0, 0, 0, 0))

    def test_invalid_geometry_or_coordinate_sets_are_rejected(self) -> None:
        cases = [
            ("missing", lambda k, v: v["layouts"]["keymap"][0].pop()),
            ("duplicate", lambda k, v: v["layouts"]["keymap"][0].append("0,0")),
            ("fraction", lambda k, v: v["layouts"]["keymap"][0][0].update(w=1.001)),
            ("rotation", lambda k, v: v["layouts"]["keymap"][0][0].update(r=15)),
            ("zero", lambda k, v: v["layouts"]["keymap"][0][0].update(w=0)),
            (
                "duplicate_matrix",
                lambda k, v: k["layouts"]["LAYOUT"]["layout"].append(
                    k["layouts"]["LAYOUT"]["layout"][0]
                ),
            ),
        ]
        for name, change in cases:
            with self.subTest(name=name):
                keyboard, via = (
                    json.loads(KEYBOARD.read_text()),
                    json.loads(VIA.read_text()),
                )
                change(keyboard, via)
                result = self.generate(keyboard, via)
                self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()

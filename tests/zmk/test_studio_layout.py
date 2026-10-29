"""Studio geometry must match VIA while preserving the firmware matrix order."""

import json
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
KEYBOARD = ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json"
VIA = ROOT / "definitions/via/ZUOYA_GMK70.json"
GENERATOR = ROOT / "firmware/zmk/tools/studio_layout.py"


type JsonValue = (
    str | int | float | bool | None | list[JsonValue] | dict[str, JsonValue]
)


class JsonFields:
    @staticmethod
    def mapping(value: JsonValue) -> dict[str, JsonValue]:
        if not isinstance(value, dict):
            raise TypeError("Expected a JSON mapping")
        return value

    @staticmethod
    def sequence(value: JsonValue) -> list[JsonValue]:
        if not isinstance(value, list):
            raise TypeError("Expected a JSON array")
        return value

    @classmethod
    def load(cls, path: Path) -> dict[str, JsonValue]:
        value: JsonValue = json.loads(path.read_text())
        return cls.mapping(value)


@dataclass(frozen=True, slots=True)
class LayoutInputs:
    keyboard: dict[str, JsonValue]
    via: dict[str, JsonValue]

    @classmethod
    def load(cls) -> "LayoutInputs":
        return cls(JsonFields.load(KEYBOARD), JsonFields.load(VIA))

    def matrix_order(self) -> list[JsonValue]:
        layouts = JsonFields.mapping(self.keyboard["layouts"])
        return JsonFields.sequence(JsonFields.mapping(layouts["LAYOUT"])["layout"])

    def first_via_row(self) -> list[JsonValue]:
        layouts = JsonFields.mapping(self.via["layouts"])
        rows = JsonFields.sequence(layouts["keymap"])
        return JsonFields.sequence(rows[0])

    def first_via_properties(self) -> dict[str, JsonValue]:
        return JsonFields.mapping(self.first_via_row()[0])


class InvalidGeometry(StrEnum):
    MISSING = "missing"
    DUPLICATE = "duplicate"
    FRACTION = "fraction"
    ROTATION = "rotation"
    ZERO = "zero"
    DUPLICATE_MATRIX = "duplicate_matrix"

    def apply(self, inputs: LayoutInputs) -> None:
        match self:
            case InvalidGeometry.MISSING:
                inputs.first_via_row().pop()
            case InvalidGeometry.DUPLICATE:
                inputs.first_via_row().append("0,0")
            case InvalidGeometry.FRACTION:
                inputs.first_via_properties()["w"] = 1.001
            case InvalidGeometry.ROTATION:
                inputs.first_via_properties()["r"] = 15
            case InvalidGeometry.ZERO:
                inputs.first_via_properties()["w"] = 0
            case InvalidGeometry.DUPLICATE_MATRIX:
                order = inputs.matrix_order()
                order.append(order[0])


class StudioLayoutTests(unittest.TestCase):
    def generate(self, inputs: LayoutInputs) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / "keyboard.json").write_text(json.dumps(inputs.keyboard))
            (base / "via.json").write_text(json.dumps(inputs.via))
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
        result = self.generate(LayoutInputs.load())
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
        inputs = LayoutInputs.load()
        inputs.first_via_properties()["w"] = 1.5
        order = inputs.matrix_order()
        order[:2] = order[1::-1]
        result = self.generate(inputs)
        self.assertEqual(result.returncode, 0, result.stderr)
        attrs = re.findall(r"<&key_physical_attrs\s+([\d ]+)>", result.stdout)
        self.assertEqual(tuple(map(int, attrs[0].split())), (100, 100, 175, 0, 0, 0, 0))
        self.assertEqual(tuple(map(int, attrs[1].split())), (150, 100, 0, 0, 0, 0, 0))

    def test_invalid_geometry_or_coordinate_sets_are_rejected(self) -> None:
        for change in InvalidGeometry:
            with self.subTest(name=change.value):
                inputs = LayoutInputs.load()
                change.apply(inputs)
                result = self.generate(inputs)
                self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()

import hashlib
import json
import unittest
from pathlib import Path


class DefinitionTests(unittest.TestCase):
    def test_factory_definition_is_preserved(self) -> None:
        root = Path(__file__).resolve().parents[3]
        data = (root / "definitions/via/ZUOYA_GMK70.json").read_bytes()
        self.assertEqual(
            hashlib.sha256(data).hexdigest(),
            "e878d51b9c1bdbf182feb48fa892ad822c9b4b8ff94f42ebe6b5e7fd0ba94789",
        )

    def test_layout_coordinates_match_board(self) -> None:
        root = Path(__file__).resolve().parents[3]
        definition = json.loads((root / "definitions/via/ZUOYA_GMK70.json").read_text())
        board = json.loads(
            (
                root / "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json"
            ).read_text()
        )
        self.assertEqual(definition["matrix"], {"rows": 10, "cols": 9})
        self.assertEqual(int(definition["vendorId"], 16), int(board["usb"]["vid"], 16))
        self.assertEqual(int(definition["productId"], 16), int(board["usb"]["pid"], 16))
        positions = [
            tuple(map(int, key.split("\n", 1)[0].split(",")))
            for row in definition["layouts"]["keymap"]
            for key in row
            if isinstance(key, str)
        ]
        self.assertEqual(
            positions,
            [tuple(key["matrix"]) for key in board["layouts"]["LAYOUT"]["layout"]],
        )


if __name__ == "__main__":
    unittest.main()

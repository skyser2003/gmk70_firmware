"""Function flip must intercept native key events and use the existing worker."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
from check_image import validate_flip_linking


class FunctionFlipImageTests(unittest.TestCase):
    symbols = {"gmk_flip_poll", "__wrap_raise_zmk_keycode_state_changed"}
    assembly = """08002100 <raise_zmk_keycode_state_changed_from_encoded>:
 8002100: f000 f87e bl 8002200 <__wrap_raise_zmk_keycode_state_changed>
08002300 <worker>:
 8002300: f000 f87e bl 8002400 <gmk_flip_poll>
"""

    def test_real_native_event_call_and_existing_worker_are_required(self) -> None:
        validate_flip_linking(self.symbols, self.assembly)
        for old, new in (
            ("<raise_zmk_keycode_state_changed_from_encoded>:", "<other>:"),
            ("<worker>:", "<other_worker>:"),
            ("bl 8002200", "ldr r0, 8002200"),
            (
                "<__wrap_raise_zmk_keycode_state_changed>",
                "<raise_zmk_keycode_state_changed>",
            ),
        ):
            with self.subTest(old=old), self.assertRaises(ValueError):
                validate_flip_linking(self.symbols, self.assembly.replace(old, new))
        with self.assertRaises(ValueError):
            validate_flip_linking({"gmk_flip_poll"}, self.assembly)

    def test_legacy_images_without_flip_are_accepted(self) -> None:
        validate_flip_linking(set(), "")

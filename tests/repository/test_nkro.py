# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise maintained NKRO hooks against both pinned native QMK frameworks."""

import json
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import TypedDict, cast

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70"


class Position(TypedDict):
    matrix: list[int]


class Layout(TypedDict):
    layout: list[Position]


class UsbConfig(TypedDict, total=False):
    force_nkro: bool


class BoardInfo(TypedDict):
    usb: UsbConfig
    layouts: dict[str, Layout]


class NkroTests(unittest.TestCase):
    @staticmethod
    def function(source: Path, signature: str) -> str:
        """Compile exact maintained bodies; never copy their implementation."""
        match = re.search(
            r"^"
            + r"\s+".join(re.escape(word) for word in signature.split())
            + r" \{.*?^\}",
            source.read_text(),
            re.MULTILINE | re.DOTALL,
        )
        if match is None:
            raise AssertionError(f"Missing function: {source}: {signature}")
        return match.group() + "\n"

    @classmethod
    def fixture(cls, framework: Path, output: Path) -> None:
        board_source = BOARD / "gmk70.c"
        info = cast(BoardInfo, json.loads((BOARD / "keyboard.json").read_text()))
        positions = info["layouts"]["LAYOUT"]["layout"]
        matrix = [["KC_NO"] * 9 for _ in range(10)]
        for index, position in enumerate(positions):
            row, col = position["matrix"]
            matrix[row][col] = f"k{index}"
        layout = "{" + ",".join("{" + ",".join(row) + "}" for row in matrix) + "}"
        body = [
            '#include "quantum_keycodes.h"',
            '#include "stock_keycodes.h"',
            '#define QMK_KEYBOARD_H "quantum_keycodes.h"',
            "#define LAYOUT("
            + ",".join(f"k{i}" for i in range(len(positions)))
            + ") "
            + layout,
            '#include "keymaps/stock_keymap.h"',
        ]
        if info["usb"].get("force_nkro", False):
            body.append("#define FORCE_NKRO")
        for signature in (
            "uint8_t gmk70_stock_flip_modifiers(void)",
            "static void stock_nkro_release_reports(void)",
            "uint8_t gmk70_stock_nkro_get(void)",
            "void gmk70_stock_nkro_set(uint8_t enabled)",
            "void hs_reset_settings(void)",
        ):
            body.append(cls.function(board_source, signature))
        for signature in (
            "__attribute__((weak)) void register_code(uint8_t code)",
            "__attribute__((weak)) void unregister_code(uint8_t code)",
            "void clear_keyboard(void)",
            "void clear_keyboard_but_mods(void)",
            "void clear_keyboard_but_mods_and_keys(void)",
        ):
            body.append(cls.function(framework / "quantum/action.c", signature))
        for signature in ("void quantum_init(void)", "void keyboard_init(void)"):
            body.append(cls.function(framework / "quantum/keyboard.c", signature))
        # Only this switch arm is isolated; its native process_magic consumer is
        # linked in full. Other board switch arms depend on unrelated hardware.
        arm = re.search(
            r"(?:        case NK_ON:.*?|        case NK_TOGG:.*?)        case RL_MOD:",
            board_source.read_text(),
            re.DOTALL,
        )
        if arm is None:
            raise AssertionError("Missing board NKRO switch arm")
        body.append(
            "static bool board_nkro_record(uint16_t keycode, keyrecord_t *record) {\n switch (keycode) {\n"
            + arm.group().removesuffix("        case RL_MOD:")
            + "default: break;\n }\nreturn true;\n}\n"
        )
        output.write_text("\n".join(body))

    def test_native_nkro_contract(self) -> None:
        for framework_name in ("qmk", "vial-qmk"):
            with (
                self.subTest(framework=framework_name),
                tempfile.TemporaryDirectory() as temporary,
            ):
                output = Path(temporary)
                framework = ROOT / "dependencies" / framework_name
                self.fixture(framework, output / "nkro_fixture.h")
                paths = (
                    output,
                    ROOT / "tests/qmk/function_flip_stubs",
                    ROOT / "tests/qmk/stubs",
                    BOARD,
                    framework,
                    framework / "quantum",
                    framework / "quantum/keymap_extras",
                    framework / "quantum/sequencer",
                    framework / "quantum/logging",
                    framework / "quantum/nvm",
                    framework / "platforms",
                    framework / "tmk_core/protocol",
                )
                command = [
                    "cc",
                    "-std=gnu11",
                    "-O1",
                    "-g",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    "-ffunction-sections",
                    "-fdata-sections",
                    "-Wl,--gc-sections",
                    "-DNKRO_ENABLE",
                    "-DEXTRAKEY_ENABLE",
                    "-DNKRO_SHARED_EP",
                    "-DNO_ACTION_ONESHOT",
                    "-DNO_DEBUG",
                    "-DNO_PRINT",
                    "-DMATRIX_ROWS=10",
                    "-DMATRIX_COLS=9",
                    "-include",
                    str(BOARD / "config.h"),
                    *("-I" + str(path) for path in paths),
                    str(ROOT / "tests/qmk/nkro_test.c"),
                    str(BOARD / "function_flip.c"),
                    str(BOARD / "function_flip_qmk.c"),
                    str(framework / "quantum/eeconfig.c"),
                    str(framework / "quantum/process_keycode/process_magic.c"),
                    str(framework / "quantum/action_util.c"),
                    str(framework / "tmk_core/protocol/report.c"),
                    "-Wl,--wrap=host_keyboard_send",
                    "-Wl,--wrap=host_nkro_send",
                    "-o",
                    str(output / "nkro"),
                ]
                subprocess.run(command, check=True, cwd=ROOT)
                for scenario in (
                    "default",
                    "saved",
                    "live",
                    "toggle",
                    "boot",
                    "flip",
                    "flip-raw",
                    "reset",
                    "keymaps",
                ):
                    with self.subTest(scenario=scenario):
                        subprocess.run(
                            [str(output / "nkro"), scenario], check=True, cwd=ROOT
                        )


if __name__ == "__main__":
    unittest.main()

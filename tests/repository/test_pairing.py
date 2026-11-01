# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise shared pairing with pinned native timers, keymaps and storage guards."""

import subprocess
import tempfile
import unittest
from pathlib import Path

import test_nkro

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70"


class PairingTests(unittest.TestCase):
    @staticmethod
    def fixture(framework: Path, output: Path) -> None:
        board = BOARD / "gmk70.c"
        body = [
            '#include "pairing_hold.h"',
            test_nkro.NkroTests.function(
                board, "static bool selected_device_allowed(uint8_t device)"
            ),
        ]
        body.append(
            test_nkro.NkroTests.function(
                board, "bool process_record_wls(uint16_t keycode, keyrecord_t *record)"
            )
        )
        (output / "pairing_board_fixture.h").write_text("\n".join(body))
        dynamic = framework / "quantum/dynamic_keymap.c"
        (output / "pairing_dynamic_fixture.c").write_text(
            '#include "pairing_storage_fixture.h"\n'
            + "\n".join(
                test_nkro.NkroTests.function(dynamic, signature)
                for signature in (
                    "void dynamic_keymap_set_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode)",
                    "void dynamic_keymap_set_buffer(uint16_t offset, uint16_t size, uint8_t *data)",
                    "void dynamic_keymap_reset(void)",
                )
            )
        )
        storage = framework / "quantum/nvm/eeprom/nvm_dynamic_keymap.c"
        (output / "pairing_nvm_fixture.c").write_text(
            '#include "pairing_storage_fixture.h"\n'
            + "\n".join(
                test_nkro.NkroTests.function(storage, signature)
                for signature in (
                    "void nvm_dynamic_keymap_erase(void)",
                    "static inline void *dynamic_keymap_key_to_eeprom_address(uint8_t layer, uint8_t row, uint8_t column)",
                    "void nvm_dynamic_keymap_update_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode)",
                    "void nvm_dynamic_keymap_update_buffer(uint32_t offset, uint32_t size, uint8_t *data)",
                )
            )
        )

    def test_pairing_guards(self) -> None:
        for framework_name in ("qmk", "vial-qmk"):
            with (
                self.subTest(framework=framework_name),
                tempfile.TemporaryDirectory() as temporary,
            ):
                output = Path(temporary)
                framework = ROOT / "dependencies" / framework_name
                self.fixture(framework, output)
                paths = (
                    output,
                    ROOT / "tests/qmk",
                    ROOT / "tests/qmk/vial_stubs",
                    ROOT / "tests/qmk/stock_battery_stubs",
                    framework / "platforms",
                    ROOT / "tests/qmk/stubs",
                    BOARD,
                    ROOT / "firmware/qmk-common/drivers/wireless",
                    framework / "quantum",
                    framework / "quantum/keymap_extras",
                    framework / "quantum/sequencer",
                    framework / "quantum/nvm",
                    framework / "tmk_core/protocol",
                )
                sources = [
                    ROOT / "tests/qmk/pairing_test.c",
                    output / "pairing_dynamic_fixture.c",
                    output / "pairing_nvm_fixture.c",
                    framework / "quantum/deferred_exec.c",
                    BOARD / "stock_keycodes.c",
                    BOARD / "hardware.c",
                    BOARD / "pairing_hold.c",
                ]
                command = [
                    "cc",
                    "-std=gnu11",
                    "-O1",
                    "-g",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-Wno-sign-compare",
                    "-Wno-int-to-pointer-cast",
                    "-Wno-old-style-declaration",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    "-ffunction-sections",
                    "-fdata-sections",
                    "-Wl,--gc-sections",
                    '-DQMK_KEYBOARD_H="quantum.h"',
                    "-DA5=5",
                    "-DA8=8",
                    "-DVIA_ENABLE",
                    "-DCOMBO_ENABLE",
                    "-DREPEAT_KEY_ENABLE",
                    "-DMATRIX_ROWS=10",
                    "-DMATRIX_COLS=9",
                    "-DDYNAMIC_KEYMAP_LAYER_COUNT=4",
                    "-include",
                    str(BOARD / "config.h"),
                    *("-I" + str(path) for path in paths),
                    *(str(source) for source in sources),
                ]
                if framework_name == "vial-qmk":
                    command.append("-DVIAL_ENABLE")
                command.extend(
                    "-Wl,--wrap=" + symbol
                    for symbol in (
                        "nvm_dynamic_keymap_update_keycode",
                        "nvm_dynamic_keymap_update_buffer",
                        "nvm_dynamic_keymap_erase",
                    )
                )
                command.extend(["-o", str(output / "pairing")])
                subprocess.run(command, check=True, cwd=ROOT)
                for scenario in (
                    "duplicate",
                    "remapped-release",
                    "single-write",
                    "buffer-write",
                    "reset",
                    "timing",
                    "replacement",
                    "mode-gate",
                    "usb",
                    "synthetic",
                    "vial-authorization",
                ):
                    with self.subTest(scenario=scenario):
                        subprocess.run(
                            [str(output / "pairing"), scenario], check=True, cwd=ROOT
                        )


if __name__ == "__main__":
    unittest.main()

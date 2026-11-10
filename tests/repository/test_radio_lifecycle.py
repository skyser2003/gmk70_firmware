# SPDX-License-Identifier: GPL-2.0-or-later
"""Run shared board gates and radio RX against both native report engines."""

import subprocess
import tempfile
import unittest
from pathlib import Path

import test_nkro

BOARD = test_nkro.BOARD
ROOT = test_nkro.ROOT


class RadioLifecycleTests(unittest.TestCase):
    @staticmethod
    def fixture(framework: Path, output: Path) -> None:
        board = BOARD / "gmk70.c"
        bodies = [
            test_nkro.NkroTests.function(board, signature)
            for signature in (
                "uint8_t gmk70_stock_flip_modifiers(void)",
                "static void stock_nkro_release_reports(void)",
                "void wireless_reports_reset_kb(void)",
                "static bool selected_device_allowed(uint8_t device)",
                "bool process_record_kb(uint16_t keycode, keyrecord_t *record)",
            )
        ]
        for signature in (
            "__attribute__((weak)) void register_code(uint8_t code)",
            "__attribute__((weak)) void unregister_code(uint8_t code)",
            "void clear_keyboard(void)",
            "void clear_keyboard_but_mods(void)",
            "void clear_keyboard_but_mods_and_keys(void)",
        ):
            bodies.append(
                test_nkro.NkroTests.function(framework / "quantum/action.c", signature)
            )
        action = (framework / "quantum/action.c").read_text()
        start = action.index(
            "void process_action(keyrecord_t *record, action_t action) {"
        )
        # Vial has unindented inner braces in conditional layer code. Select the
        # complete native function through the following documentation block.
        bodies.append(action[start : action.index("\n/**", start)])
        bodies.append(
            test_nkro.NkroTests.function(
                BOARD / "wls/wls.c",
                "enum modeio_mode hs_modeio_read(void)",
            )
        )
        (output / "radio_lifecycle_fixture.h").write_text("\n".join(bodies))

    def test_native_radio_lifecycle(self) -> None:
        for name in ("qmk", "vial-qmk"):
            print(f"Native radio lifecycle: {name}", flush=True)
            with self.subTest(framework=name), tempfile.TemporaryDirectory() as temp:
                output = Path(temp)
                framework = ROOT / "dependencies" / name
                self.fixture(framework, output)
                includes = (
                    output,
                    ROOT / "tests/qmk/radio_lifecycle_stubs",
                    ROOT / "tests/qmk/function_flip_stubs",
                    ROOT / "tests/qmk/stubs",
                    ROOT / "tests/qmk/module_stubs",
                    BOARD,
                    ROOT / "firmware/qmk-common/drivers/wireless",
                    framework,
                    framework / "quantum",
                    framework / "quantum/keymap_extras",
                    framework / "quantum/sequencer",
                    framework / "quantum/logging",
                    framework / "quantum/nvm",
                    framework / "platforms",
                    framework / "tmk_core/protocol",
                )
                sources = (
                    ROOT / "tests/qmk/radio_lifecycle_test.c",
                    BOARD / "function_flip.c",
                    BOARD / "function_flip_qmk.c",
                    BOARD / "stock_keycodes.c",
                    ROOT / "firmware/qmk-common/drivers/wireless/module.c",
                    ROOT / "firmware/qmk-common/drivers/wireless/wireless.c",
                    ROOT / "firmware/qmk-common/drivers/wireless/transport.c",
                    ROOT / "firmware/qmk-common/drivers/wireless/radio_keys.c",
                    ROOT / "firmware/qmk-common/drivers/wireless/smsg.c",
                    framework / "quantum/action_util.c",
                    framework / "tmk_core/protocol/report.c",
                    framework / "tmk_core/protocol/host.c",
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
                    "-fno-pie",
                    "-no-pie",
                    "-ffunction-sections",
                    "-fdata-sections",
                    "-Wl,--gc-sections",
                    "-DNKRO_ENABLE",
                    "-DEXTRAKEY_ENABLE",
                    "-DNKRO_SHARED_EP",
                    "-DNO_ACTION_ONESHOT",
                    "-DNO_ACTION_TAPPING",
                    "-DNO_ACTION_LAYER",
                    "-DNO_ACTION_MACRO",
                    "-DNO_ACTION_FUNCTION",
                    "-DNO_DEBUG",
                    "-DNO_PRINT",
                    "-DKEEP_USB_CONNECTION_IN_WIRELESS_MODE",
                    "-DHS_BAT_CABLE_PIN=7",
                    "-DMATRIX_ROWS=10",
                    "-DMATRIX_COLS=9",
                    "-DC14=14",
                    "-DC15=15",
                    '-DPRODUCT="ZUOYA GMK70"',
                    '-DMANUFACTURER="HS"',
                    "-DVENDOR_ID=0x342d",
                    "-DPRODUCT_ID=0xe491",
                    '-DQMK_KEYBOARD_H="quantum.h"',
                    *("-I" + str(path) for path in includes),
                    *(str(source) for source in sources),
                    "-Wl,--wrap=host_keyboard_send",
                    "-Wl,--wrap=host_nkro_send",
                    "-o",
                    str(output / "lifecycle"),
                ]
                subprocess.run(command, check=True, cwd=ROOT)
                for nkro in ("0", "1"):
                    for scenario in (
                        "released",
                        "held",
                        "pairing",
                        "pairing-cache",
                        "consumer-cache",
                        "reject",
                        "modifiers",
                        "blocked-press",
                        "flip",
                        "repeated",
                        "switch",
                        "reset",
                        "usb-switch",
                        "same-channel",
                        "physical-mode",
                    ):
                        with self.subTest(nkro=nkro, scenario=scenario):
                            subprocess.run(
                                [str(output / "lifecycle"), scenario, nkro],
                                check=True,
                                cwd=ROOT,
                            )


if __name__ == "__main__":
    unittest.main()

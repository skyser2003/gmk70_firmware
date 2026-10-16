"""Link real framework resume code to GMK70's delayed wake and board hooks."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70"


def extract_function(source: str, signature: str) -> str:
    begin = source.index(signature)
    end = source.index("{", begin) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end] + "\n"


class WakeInputTests(unittest.TestCase):
    def check_framework(self, framework: str) -> None:
        dependency = ROOT / "dependencies" / framework
        board = (BOARD / "gmk70.c").read_text()
        quantum = (dependency / "quantum/quantum.c").read_text()
        hooks = (
            '#include "wake_input_fixture.h"\n#include "wls/wls.h"\n'
            + extract_function(
                quantum,
                "__attribute__((weak)) void suspend_wakeup_init_quantum(void)",
            )
        )
        for name in (
            "suspend_wakeup_init_kb",
            "suspend_wakeup_init_user",
            "lpwr_wakeup_hook",
        ):
            hooks += extract_function(board, f"void {name}(void)")
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for name in (
                "ch",
                "hal",
                "matrix",
                "action",
                "action_util",
                "mousekey",
                "programmable_button",
                "host",
                "suspend",
                "led",
                "wait",
            ):
                (folder / f"{name}.h").write_text('#include "wake_input_fixture.h"\n')
            (folder / "hooks.c").write_text(hooks)
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
                "-DGMK70_TEST_FRAMEWORK_WAKE",
                "-DRGB_MATRIX_ENABLE",
                "-DMOUSEKEY_ENABLE",
                "-DPROGRAMMABLE_BUTTON_ENABLE",
                "-DEXTRAKEY_ENABLE",
                '-DQMK_KEYBOARD_H="quantum.h"',
                "-I" + str(folder),
            ]
            for include in (
                "tests/qmk/wake_stubs",
                "tests/qmk/lowpower_stubs",
                "tests/qmk/stubs",
                "tests/qmk",
                "firmware/qmk-common/drivers/wireless",
                "firmware/qmk-common/keyboards/zuoya/gmk70",
                f"dependencies/{framework}/quantum",
                f"dependencies/{framework}/tmk_core/protocol",
            ):
                command.append("-I" + str(ROOT / include))
            command += [
                str(ROOT / "tests/qmk/wake_input_test.c"),
                str(ROOT / "firmware/qmk-common/drivers/wireless/lowpower.c"),
                str(BOARD / "wls/wls.c"),
                str(dependency / "platforms/chibios/suspend.c"),
                str(folder / "hooks.c"),
                "-o",
                str(folder / "wake-input"),
            ]
            compiled = subprocess.run(
                command, capture_output=True, text=True, check=False
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            for case in (
                "alt-tab",
                "held-input",
                "released-input",
                "usb-resume",
                "usb-control",
                "host-switch",
            ):
                with self.subTest(framework=framework, case=case):
                    result = subprocess.run(
                        [str(folder / "wake-input"), case],
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )

    def test_qmk_wake_input(self) -> None:
        self.check_framework("qmk")

    def test_vial_wake_input(self) -> None:
        self.check_framework("vial-qmk")

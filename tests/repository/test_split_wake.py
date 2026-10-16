"""Run the actual board wake callback against retained-idle-high split GPIO."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "firmware/qmk-common/keyboards/zuoya/gmk70"


class SplitWakeTests(unittest.TestCase):
    def test_right_key_wakes_sleeping_master(self) -> None:
        source = (BOARD / "gmk70.c").read_text()
        begin = source.index("void suspend_wakeup_init_user(void) {")
        end = source.index("\nvoid suspend_power_down_user(void)", begin)
        # Keep the wiring to the real wake callback in the test, rather than
        # calling a helper which could be accidentally omitted from firmware.
        callback = '#include "quantum.h"\n#include "wls/wls.h"\n' + source[begin:end]
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "board_wake.c").write_text(callback)
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
                "-DGMK70_TEST_BOARD_WAKE",
                '-DQMK_KEYBOARD_H="quantum.h"',
            ]
            for include in (
                "tests/qmk/wake_stubs",
                "tests/qmk/stubs",
                "tests/qmk",
                "firmware/qmk-common/drivers/wireless",
                "firmware/qmk-common/keyboards/zuoya/gmk70",
                "dependencies/qmk/quantum",
                "dependencies/qmk/quantum/keymap_extras",
                "dependencies/qmk/tmk_core/protocol",
            ):
                command.append("-I" + str(ROOT / include))
            command += [
                str(ROOT / "tests/qmk/wake_test.c"),
                str(ROOT / "firmware/qmk-common/drivers/wireless/lpwr_wb32.c"),
                str(BOARD / "wls/wls.c"),
                str(folder / "board_wake.c"),
                "-o",
                str(folder / "wake"),
            ]
            compiled = subprocess.run(
                command, capture_output=True, text=True, check=False
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run(
                [str(folder / "wake")], capture_output=True, text=True, check=False
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

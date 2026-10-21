"""Observe real radio parsing without changing its accepted packets."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class RadioCaptureTests(unittest.TestCase):
    def test_vial_settings_and_usb_gates(self) -> None:
        def extract(source: str, signature: str) -> str:
            begin = source.index(signature)
            end = source.index("{", begin) + 1
            depth = 1
            while depth:
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
            return source[begin:end] + "\n"

        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "ch.h").write_text(
                "#pragma once\nvoid chSysLock(void);\nvoid chSysUnlock(void);\n"
            )
            (folder / "usb_main.h").write_text(
                "#pragma once\nstruct test_usb_driver {int state;};\n"
                "extern struct test_usb_driver test_usb;\n#define USB_DRIVER test_usb\n"
                "#define USB_ACTIVE 2\nbool is_keyboard_left(void);\n"
            )
            (folder / "via.h").write_text(
                "#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n"
                "#define VIAL_ENABLE\n#define BUILD_ID ((uint32_t)0x00AABBCC)\n"
                "#define VIA_EEPROM_MAGIC_ADDR 0x21\nvoid via_init_kb(void);\n"
                "uint32_t via_get_layout_options(void);\nvoid via_set_layout_options_kb(uint32_t);\n"
                "void eeconfig_init_via(void);\nvoid via_eeprom_set_valid(bool);\n"
                "bool via_eeprom_is_valid(void);\n"
            )
            native = (ROOT / "dependencies/vial-qmk/quantum/via.c").read_text()
            functions = '#include "via.h"\n#include "eeprom.h"\n'
            for name in (
                "bool via_eeprom_is_valid(void)",
                "void via_eeprom_set_valid(bool valid)",
                "void via_init(void)",
            ):
                functions += extract(native, name)
            (folder / "native.c").write_text(functions)
            (folder / "startup.h").write_text(
                "#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n"
                "typedef uint32_t layer_state_t;\n"
                "extern uint8_t debug_config, keymap_config;\n"
                "extern layer_state_t layer_state;\n"
                "bool nvm_eeconfig_is_enabled(void);\n"
                "bool nvm_eeconfig_is_disabled(void);\n"
                "bool eeconfig_is_enabled(void);\nbool eeconfig_is_disabled(void);\n"
                "void eeconfig_init(void);\nvoid quantum_init(void);\n"
                "void eeconfig_read_debug(uint8_t *);\n"
                "void eeconfig_read_keymap(uint8_t *);\n"
                "layer_state_t eeconfig_read_default_layer(void);\n"
                "void default_layer_set(layer_state_t);\n"
                "void layer_state_set_kb(layer_state_t);\n"
            )
            eeconfig = (ROOT / "dependencies/vial-qmk/quantum/eeconfig.c").read_text()
            (folder / "native_eeconfig.c").write_text(
                '#include "startup.h"\n#include "via.h"\n'
                + extract(eeconfig, "bool eeconfig_is_enabled(void)")
                + extract(eeconfig, "bool eeconfig_is_disabled(void)")
            )
            keyboard = (ROOT / "dependencies/vial-qmk/quantum/keyboard.c").read_text()
            (folder / "native_keyboard.c").write_text(
                '#include "startup.h"\n' + extract(keyboard, "void quantum_init(void)")
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
                "-DGMK70_RADIO_DIAGNOSTICS",
                "-DVIA_ENABLE",
                "-DEEPROM_TEST_HARNESS",
                "-DCOMBO_ENABLE",
                "-DREPEAT_KEY_ENABLE",
                "-DMATRIX_ROWS=10",
                "-DMATRIX_COLS=9",
                "-I" + str(folder),
            ]
            for include in (
                "tests/qmk/vial_stubs",
                "tests/qmk/stubs",
                "tests/qmk",
                "firmware/qmk-common/drivers/wireless",
                "firmware/qmk-common/keyboards/zuoya/gmk70",
                "dependencies/vial-qmk/quantum/nvm",
                "dependencies/qmk/quantum",
                "dependencies/qmk/tmk_core/protocol",
                "dependencies/qmk/platforms",
            ):
                command.append("-I" + str(ROOT / include))
            command += [
                str(ROOT / source)
                for source in (
                    "tests/qmk/radio_vial_test.c",
                    "tests/qmk/stock_v15_fixture.c",
                    "firmware/qmk-vial/compat/vial_adapter.c",
                    "firmware/qmk-common/keyboards/zuoya/gmk70/stock_power.c",
                    "firmware/qmk-common/drivers/wireless/radio_capture.c",
                )
            ]
            command += [
                str(folder / "native.c"),
                str(folder / "native_eeconfig.c"),
                str(folder / "native_keyboard.c"),
                "-Wl,--wrap=via_init",
                "-Wl,--wrap=via_eeprom_is_valid",
                "-o",
                str(folder / "vial"),
            ]
            built = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run(
                [str(folder / "vial")], capture_output=True, text=True, check=False
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_parser_capture_and_read_only_query(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "ch.h").write_text(
                "#pragma once\n"
                "static inline void chSysLock(void) {}\n"
                "static inline void chSysUnlock(void) {}\n"
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
                "-DGMK70_RADIO_DIAGNOSTICS",
                '-DPRODUCT="ZUOYA GMK70"',
                '-DMANUFACTURER="HS"',
                "-DVENDOR_ID=0x342d",
                "-DPRODUCT_ID=0xe491",
                "-I" + str(folder),
            ]
            for include in (
                "tests/qmk/module_stubs",
                "tests/qmk/stubs",
                "tests/qmk",
                "firmware/qmk-common/drivers/wireless",
                "firmware/qmk-common/keyboards/zuoya/gmk70",
                "dependencies/qmk/quantum",
                "dependencies/qmk/tmk_core/protocol",
            ):
                command.append("-I" + str(ROOT / include))
            for source in (
                "tests/qmk/radio_capture_test.c",
                "firmware/qmk-common/drivers/wireless/module.c",
                "firmware/qmk-common/drivers/wireless/smsg.c",
                "firmware/qmk-common/drivers/wireless/radio_capture.c",
            ):
                if (ROOT / source).exists():
                    command.append(str(ROOT / source))
            command += ["-o", str(folder / "capture")]
            built = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run(
                [str(folder / "capture")], capture_output=True, text=True, check=False
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

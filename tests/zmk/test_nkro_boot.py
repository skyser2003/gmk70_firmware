# SPDX-License-Identifier: GPL-2.0-or-later
"""Check boot-report recovery against pinned native HID and the project adapter."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / "dependencies/zmk/app/src/hid.c"


class NkroBootTests(unittest.TestCase):
    @staticmethod
    def function(source: str, signature: str) -> str:
        match = re.search(r"^" + re.escape(signature) + r"\s*\{", source, re.MULTILINE)
        if match is None:
            raise AssertionError(f"Missing native function: {signature}")
        depth = 1
        end = match.end()
        while depth:
            if end == len(source):
                raise AssertionError(f"Unterminated native function: {signature}")
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        return source[match.start() : end] + "\n"

    @classmethod
    def native_fixture(cls, destination: Path) -> None:
        source = NATIVE.read_text()
        keys = (ROOT / "dependencies/zmk/app/include/zmk/keys.h").read_text()
        typedefs = re.findall(
            r"^typedef uint(?:8|32)_t zmk_(?:key_t|mod_t|mod_flags_t);",
            keys,
            re.MULTILINE,
        )
        if len(typedefs) != 3:
            raise AssertionError("Native HID key and modifier types changed")
        prelude = source[
            source.index(
                "static struct zmk_hid_keyboard_report keyboard_report"
            ) : source.index("zmk_mod_flags_t zmk_hid_get_explicit_mods")
        ]
        macro = re.search(
            r"^#define TOGGLE_KEYBOARD\(code, val\).*$", source, re.MULTILINE
        )
        if macro is None:
            raise AssertionError("Native NKRO bitmap macro missing")
        body = [
            '#include "nkro_boot_native.h"',
            *typedefs,
            prelude,
            macro.group(),
        ]
        for signature in (
            "zmk_mod_flags_t zmk_hid_get_explicit_mods(void)",
            "int zmk_hid_register_mod(zmk_mod_t modifier)",
            "int zmk_hid_unregister_mod(zmk_mod_t modifier)",
            "static zmk_hid_boot_report_t *boot_report_rollover(uint8_t modifiers)",
            "zmk_hid_boot_report_t *zmk_hid_get_boot_report(void)",
            "static inline int select_keyboard_usage(zmk_key_t usage)",
            "static inline int deselect_keyboard_usage(zmk_key_t usage)",
            "int zmk_hid_keyboard_press(zmk_key_t code)",
            "int zmk_hid_keyboard_release(zmk_key_t code)",
            "void zmk_hid_keyboard_clear(void)",
            "void zmk_hid_consumer_clear(void)",
            "struct zmk_hid_keyboard_report *zmk_hid_get_keyboard_report(void)",
            "struct zmk_hid_consumer_report *zmk_hid_get_consumer_report(void)",
        ):
            body.append(cls.function(source, signature))
        # Before the project adapter supplies its strong getter, this fallback
        # exposes the native defect through the same real GNU linker boundary.
        body.append(
            "#ifdef TEST_ADAPTER\n"
            "zmk_hid_boot_report_t *__real_zmk_hid_get_boot_report(void);\n"
            "__attribute__((weak)) zmk_hid_boot_report_t *__wrap_zmk_hid_get_boot_report(void) {\n"
            "return __real_zmk_hid_get_boot_report();\n}\n#endif\n"
        )
        destination.write_text("\n".join(body))

    def run_profile(self, adapter: bool) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            native = output / "native_hid.c"
            self.native_fixture(native)
            command = [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-sign-compare",
                "-O1",
                "-g",
                "-fno-pie",
                "-no-pie",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-DCONFIG_ZMK_USB_BOOT=1",
                "-DCONFIG_ZMK_HID_REPORT_TYPE_NKRO=1",
                "-Itests/zmk",
                "-Itests/zmk/function_flip_stubs",
                "-Itests/zmk/radio_kernel_stubs",
                "-Itests/zmk/radio_endpoint_stubs",
                "-Idependencies/zmk/app/include",
                "-Ifirmware/zmk/update",
                "-Ifirmware/qmk-common/keyboards/zuoya/gmk70/update",
                str(native),
                "tests/zmk/nkro_boot_test.c",
            ]
            if adapter:
                command.extend(
                    [
                        "-DTEST_ADAPTER",
                        "-DCONFIG_GMK70_RADIO=1",
                        "firmware/zmk/studio/bool_setting.c",
                        "firmware/qmk-common/drivers/wireless/radio_keys.c",
                        "-Wl,--wrap=zmk_hid_get_boot_report",
                        "-Wl,--wrap=zmk_hid_get_keyboard_report",
                    ]
                )
            command.extend(["-o", str(output / "boot")])
            subprocess.run(command, check=True, cwd=ROOT)
            for scenario in ("clear", "duplicate", "duplicate-b", "bitmap"):
                with self.subTest(scenario=scenario):
                    subprocess.run(
                        [str(output / "boot"), scenario], check=True, cwd=ROOT
                    )

    def test_native_baseline_exposes_stale_and_underflowed_boot_counter(self) -> None:
        self.run_profile(adapter=False)

    def test_project_boot_adapter_recovers_from_native_counter_errors(self) -> None:
        self.run_profile(adapter=True)


if __name__ == "__main__":
    unittest.main()

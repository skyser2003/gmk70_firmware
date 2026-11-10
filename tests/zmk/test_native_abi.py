"""Link the maintained non-LTO thunks against LTO callers and native substitutes."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXPORTS = {
    "rgb_underglow_on": ("void", "", "31", "", 31),
    "rgb_underglow_off": ("void", "", "32", "", 32),
    "rgb_underglow_toggle": ("void", "", "33", "", 33),
    "rgb_underglow_set_hsb": (
        "struct zmk_led_hsb color",
        "color",
        "color.h + color.s + color.b",
        "((struct zmk_led_hsb){42, 73, 91})",
        206,
    ),
    "endpoint_send_report": ("uint16_t page", "page", "page + 1", "7", 8),
    "endpoint_send_mouse_report": ("void", "", "2", "", 2),
    "keymap_set_layer_binding_at_idx": (
        "zmk_keymap_layer_id_t layer, uint16_t index, const struct zmk_behavior_binding binding",
        "layer, index, binding",
        "layer + index + binding.local_id + binding.param1 + binding.param2 + binding.behavior_dev[0]",
        '3, 73, ((struct zmk_behavior_binding){5, "k", 40, 50})',
        278,
    ),
    "keymap_discard_changes": ("void", "", "11", "", 11),
    "keymap_reset_settings": ("void", "", "12", "", 12),
    "keymap_add_layer": ("void", "", "13", "", 13),
    "keymap_remove_layer": (
        "zmk_keymap_layer_index_t index",
        "index",
        "14 + index",
        "3",
        17,
    ),
    "keymap_move_layer": (
        "zmk_keymap_layer_index_t start, zmk_keymap_layer_index_t end",
        "start, end",
        "15 + start * 100 + end",
        "2, 3",
        218,
    ),
    "keymap_restore_layer": (
        "zmk_keymap_layer_id_t id, zmk_keymap_layer_index_t index",
        "id, index",
        "16 + id * 100 + index",
        "2, 3",
        219,
    ),
}


class NativeAbiTests(unittest.TestCase):
    def test_direct_real_pointer_internal_calls_and_struct_forwarding(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            include = temp / "zmk"
            include.mkdir()
            (include / "behavior.h").write_text(
                "#pragma once\n#include <stdint.h>\n"
                "struct zmk_behavior_binding { uint16_t local_id; const char *behavior_dev; uint32_t param1, param2; };\n"
            )
            (include / "keymap.h").write_text(
                '#pragma once\n#include "behavior.h"\n'
                "typedef uint8_t zmk_keymap_layer_id_t;\n"
                "typedef uint8_t zmk_keymap_layer_index_t;\n"
                + "\n".join(
                    f"int zmk_{name}({arguments});"
                    for name, (arguments, *_) in EXPORTS.items()
                    if name.startswith("keymap_")
                )
            )
            (include / "endpoints.h").write_text(
                "#pragma once\n#include <stdint.h>\n#define IS_ENABLED(x) (x)\n"
                "int zmk_endpoint_send_report(uint16_t page);\n"
                "int zmk_endpoint_send_mouse_report(void);\n"
                "void zmk_endpoint_clear_reports(void);\n"
            )
            (include / "rgb_underglow.h").write_text(
                "#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n"
                "struct zmk_led_hsb { uint16_t h; uint8_t s, b; };\n"
                + "\n".join(
                    f"int zmk_{name}({arguments});"
                    for name, (arguments, *_) in EXPORTS.items()
                    if name.startswith("rgb_underglow_")
                )
            )
            headers = "#include <zmk/behavior.h>\n#include <zmk/keymap.h>\n#include <zmk/endpoints.h>\n#include <zmk/rgb_underglow.h>\n"
            native = headers + "\n".join(
                f"int zmk_{name}({arguments}) {{ return {result}; }}"
                for name, (arguments, _, result, _, _) in EXPORTS.items()
            )
            native += "\nint native_internal(void) { return zmk_keymap_add_layer() + zmk_endpoint_send_report(7); }\n"
            (temp / "native.c").write_text(native)
            caller = headers + "int native_internal(void);\n"
            checks = ["native_internal() != 21"]
            for name, (arguments, forward, _, values, expected) in EXPORTS.items():
                caller += f"int __real_zmk_{name}({arguments});\n"
                caller += f"int __wrap_zmk_{name}({arguments}) {{ return __real_zmk_{name}({forward}) + 10000; }}\n"
                checks += [
                    f"zmk_{name}({values}) != {expected + 10000}",
                    f"__real_zmk_{name}({values}) != {expected}",
                ]
            caller += "int main(void) {\n"
            caller += "int (*volatile public_pointer)(void) = zmk_keymap_add_layer;\n"
            caller += (
                "int (*volatile real_pointer)(void) = __real_zmk_keymap_add_layer;\n"
            )
            checks += ["public_pointer() != 10013", "real_pointer() != 13"]
            caller += "return " + " ||\n".join(checks) + ";\n}\n"
            (temp / "caller.c").write_text(caller)
            flags = [
                "-std=gnu11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DCONFIG_ZMK_POINTING=1",
                f"-I{temp}",
            ]
            objects = []
            sources = [
                temp / "native.c",
                temp / "caller.c",
                ROOT / "firmware/zmk/runtime/native_endpoints.c",
                ROOT / "firmware/zmk/runtime/native_keymap.c",
                ROOT / "firmware/zmk/runtime/native_rgb.c",
            ]
            for index, source in enumerate(sources):
                output = temp / f"{index}.o"
                options = ["-flto"] if index < 2 else ["-fno-lto"]
                if index == 0:
                    options += [f"-Dzmk_{name}=gmk_native_{name}" for name in EXPORTS]
                subprocess.run(
                    ["cc", *flags, *options, "-c", str(source), "-o", str(output)],
                    check=True,
                    capture_output=True,
                    text=True,
                )
                objects.append(str(output))
            program = temp / "native-abi"
            subprocess.run(
                [
                    "cc",
                    "-flto",
                    *objects,
                    *(f"-Wl,--wrap=zmk_{name}" for name in EXPORTS),
                    "-o",
                    str(program),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(program)], check=True, capture_output=True, text=True)

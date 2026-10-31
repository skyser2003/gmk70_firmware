"""Exercise pinned upstream generators against maintained external sources."""

import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
ADAPTER = PROJECT / "tools/build-support/qmk_generate.py"


class ExternalGeneratorTests(unittest.TestCase):
    def make(self, framework: str, output: Path, *arguments: str) -> str:
        variant = "vial" if framework == "vial-qmk" else "release"
        result = subprocess.run(
            [
                "make",
                "-r",
                "-R",
                "-f",
                str(PROJECT / "tools/build-support/external.mk"),
                "KEYBOARD=zuoya/gmk70",
                f"KEYMAP={variant}",
                f"GMK70_PLATFORM={'vial' if variant == 'vial' else 'qmk'}",
                f"BUILD_DIR={output}/.build",
                f"QMK_BIN={shlex.quote(sys.executable)} {shlex.quote(str(ADAPTER))}",
                "SKIP_VERSION=yes",
                "SKIP_GIT=yes",
                # These tests exercise the real Make dependency graph with
                # test recipes; native cc suffices for compiler-option probes.
                "CC=cc",
                *arguments,
            ],
            cwd=PROJECT / "dependencies" / framework,
            env={
                **os.environ,
                "GMK70_PROJECT_ROOT": str(PROJECT),
                "GMK70_KEYMAP": variant,
            },
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_vial_external_keymap_keeps_native_features(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result = self.make(
                "vial-qmk",
                Path(temporary),
                "--eval=gmk70-test-config: ; @printf '%s\\n' 'FEATURES=$(VIAL_ENABLE) $(VIALRGB_ENABLE) $(ENCODER_MAP_ENABLE) $(REPEAT_KEY_ENABLE)'",
                "gmk70-test-config",
            )
            self.assertIn("FEATURES=yes yes yes yes", result)

    def test_frameworks_select_separate_rgb_and_protocol_integration(self) -> None:
        for framework, platform, adapter in (
            ("qmk", "via", "stock_via.c"),
            ("vial-qmk", "vial", "vial_adapter.c"),
        ):
            with (
                self.subTest(framework=framework),
                tempfile.TemporaryDirectory() as temporary,
            ):
                result = self.make(
                    framework,
                    Path(temporary),
                    "--eval=gmk70-test-integration: ; @printf '%s\\n' 'RGB_SOURCE=$(GMK70_PLATFORM_DIR)/compat/rgb_integration.c' 'SOURCES=$(SRC)' 'LINK_FLAGS=$(LDFLAGS)'",
                    "gmk70-test-integration",
                )
                self.assertIn(
                    f"RGB_SOURCE={PROJECT}/firmware/qmk-{platform}/compat/rgb_integration.c",
                    result,
                )
                sources = next(
                    line.removeprefix("SOURCES=")
                    for line in result.splitlines()
                    if line.startswith("SOURCES=")
                ).split()
                self.assertIn("rgb_integration.c", sources)
                self.assertIn(adapter, sources)
                other = "vial_adapter.c" if platform == "via" else "stock_via.c"
                self.assertNotIn(other, sources)
                self.assertEqual("stock_eeprom.c" in sources, platform == "via")
                self.assertIn("-Wl,--wrap=rgb_matrix_set_color", result)

    def test_both_frameworks_enable_common_hardware_hooks(self) -> None:
        for framework in ("qmk", "vial-qmk"):
            with (
                self.subTest(framework=framework),
                tempfile.TemporaryDirectory() as temporary,
            ):
                result = self.make(
                    framework,
                    Path(temporary),
                    "--eval=gmk70-test-hardware: ; @printf '%s\\n' 'SOURCES=$(SRC)' 'LINK_FLAGS=$(LDFLAGS)'",
                    "gmk70-test-hardware",
                )
                sources = next(
                    line.removeprefix("SOURCES=")
                    for line in result.splitlines()
                    if line.startswith("SOURCES=")
                ).split()
                self.assertEqual(sources.count("hardware.c"), 1)
                self.assertNotIn("battery_indicators.c", sources)
                for task in ("housekeeping_task", "rgb_matrix_task"):
                    self.assertIn(f"-Wl,--wrap={task}", result)

    def test_linker_script_edit_invalidates_cached_elf(self) -> None:
        for framework in ("qmk", "vial-qmk"):
            with (
                self.subTest(framework=framework),
                tempfile.TemporaryDirectory() as tmp,
            ):
                root = Path(tmp)
                script = root / "selected.ld"
                script.write_text("old layout")
                elf = root / ".build/fixture.elf"
                elf.parent.mkdir()
                elf.write_text("cached application")
                os.utime(script, ns=(1_000_000_000, 1_000_000_000))
                os.utime(elf, ns=(2_000_000_000, 2_000_000_000))
                arguments = (
                    "TARGET=fixture",
                    f"LDSCRIPT={script}",
                    "--eval=$(BUILD_DIR)/fixture.elf: ; @printf '%s' 'relinked application' > $@",
                    str(elf),
                )
                self.make(framework, root, *arguments)
                self.assertEqual(elf.read_text(), "cached application")
                script.write_text("new layout")
                self.make(framework, root, *arguments)
                self.assertEqual(elf.read_text(), "relinked application")

    def generate(self, framework: str, command: str, output: Path) -> str:
        result = subprocess.run(
            [
                sys.executable,
                str(ADAPTER),
                command,
                "--quiet",
                "--keyboard",
                "zuoya/gmk70",
                "--output",
                str(output),
                *(
                    ["--keymap", "vial" if framework == "vial-qmk" else "release"]
                    if command == "generate-make-dependencies"
                    else []
                ),
            ],
            cwd=PROJECT / "dependencies" / framework,
            env={
                **os.environ,
                "GMK70_PROJECT_ROOT": str(PROJECT),
                "GMK70_KEYMAP": "vial" if framework == "vial-qmk" else "release",
            },
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return output.read_text()

    def test_upstream_generators_accept_external_board_and_keymap(self) -> None:
        for framework in ("qmk", "vial-qmk"):
            with (
                self.subTest(framework=framework),
                tempfile.TemporaryDirectory() as tmp,
            ):
                output = Path(tmp) / "generated"
                config = self.generate(framework, "generate-config-h", output)
                self.assertIn("#    define MATRIX_ROWS 10", config)
                self.assertIn("#    define MATRIX_COLS 9", config)
                rules = self.generate(framework, "generate-rules-mk", output)
                self.assertIn("MCU ?= WB32FQ95", rules)
                header = self.generate(framework, "generate-keyboard-h", output)
                self.assertIn("#define LAYOUT(", header)
                source = self.generate(framework, "generate-keyboard-c", output)
                self.assertIn("chordal_hold_layout", source)
                introspection = self.generate(
                    framework, "generate-community-modules-introspection-h", output
                )
                self.assertIn(
                    "SPDX-License-Identifier: GPL-2.0-or-later", introspection
                )
                dependencies = self.generate(
                    framework, "generate-make-dependencies", output
                )
                self.assertIn(
                    "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json",
                    dependencies,
                )
                platform = "vial" if framework == "vial-qmk" else "via"
                keymap = "vial" if platform == "vial" else "release"
                self.assertIn(
                    f"firmware/qmk-{platform}/keymaps/{keymap}/config.h", dependencies
                )

    def test_adapter_rejects_device_commands(self) -> None:
        result = subprocess.run(
            [sys.executable, str(ADAPTER), "flash"],
            cwd=PROJECT / "dependencies/qmk",
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Only build metadata generators", result.stderr)


if __name__ == "__main__":
    unittest.main()

"""Exercise each platform's RGB boundary against its real pinned RGB routing."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HEADER = """
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define RGB_MATRIX_LED_COUNT 77
#define RGB_MATRIX_SPLIT {36, 41}
bool is_keyboard_left(void);
struct rgb_fixture_driver {
    void (*set_color)(int, uint8_t, uint8_t, uint8_t);
};
extern const struct rgb_fixture_driver rgb_matrix_driver;
void rgb_matrix_set_color(int, uint8_t, uint8_t, uint8_t);
"""

CLIENT = """
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool left;
static unsigned writes;
static uint8_t frame[77][3];
bool is_keyboard_left(void) { return left; }
static void capture(int index, uint8_t r, uint8_t g, uint8_t b) {
    assert(index >= 0 && index < 77);
    frame[index][0] = r;
    frame[index][1] = g;
    frame[index][2] = b;
    ++writes;
}
const struct rgb_fixture_driver rgb_matrix_driver = {capture};
int main(void) {
    const int bt_indicators[] = {14, 13, 12};
    left = false;
    for (unsigned slot = 0; slot < 3; ++slot) {
        memset(frame, 0x55, sizeof(frame));
        writes = 0;
        rgb_matrix_set_color(bt_indicators[slot], 0, 0, 255);
        assert(writes == 0); // Right P/[ /] must keep their ordinary animation.
        for (unsigned i = 0; i < sizeof(frame); ++i)
            assert(((uint8_t *)frame)[i] == 0x55);
    }
    // Test every global LED, including caps, GUI lock, battery and logo LEDs.
    for (unsigned half = 0; half < 2; ++half) {
        left = half == 0;
        for (int index = 0; index < 77; ++index) {
            memset(frame, 0x55, sizeof(frame));
            writes = 0;
            rgb_matrix_set_color(index, 1, 2, 3);
            bool owned = (index < 36) == left;
            assert(writes == (owned ? 1u : 0u));
            for (int pixel = 0; pixel < 77; ++pixel) {
                uint8_t expected[3] = {0x55, 0x55, 0x55};
                if (owned && pixel == (left ? index : index - 36)) {
                    expected[0] = 1; expected[1] = 2; expected[2] = 3;
                }
                assert(memcmp(frame[pixel], expected, 3) == 0);
            }
        }
    }
#ifdef TEST_VIAL
    writes = 0;
    rgb_matrix_set_color(-1, 1, 2, 3);
    rgb_matrix_set_color(77, 1, 2, 3);
    rgb_matrix_set_color(255, 1, 2, 3);
    assert(writes == 0);
#endif
    puts("PASS: Bluetooth indicators, every LED on both halves and local routing");
    return 0;
}
"""


class RgbIntegrationTests(unittest.TestCase):
    def check_platform(self, platform: str, framework: str) -> None:
        source = (
            ROOT / "dependencies" / framework / "quantum/rgb_matrix/rgb_matrix.c"
        ).read_text()
        begin = source.index(
            "__attribute__((weak)) int rgb_matrix_led_index(int index)"
        )
        end = source.index("void rgb_matrix_set_color_all(", begin)
        # Compile the original index/setter functions, without maintaining a
        # copied framework implementation or a mock of their ownership rules.
        native = (
            "const uint8_t k_rgb_matrix_split[2] = RGB_MATRIX_SPLIT;\n"
            + source[begin:end]
        )
        implementation = ROOT / f"firmware/qmk-{platform}/compat/rgb_integration.c"
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "fixture.h").write_text(HEADER)
            for header in ("quantum.h", "rgb_matrix.h"):
                (folder / header).write_text('#include "fixture.h"\n')
            (folder / "native.c").write_text(native)
            (folder / "client.c").write_text(CLIENT)
            command = [
                "cc",
                "-std=gnu11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-include",
                str(folder / "fixture.h"),
                "-I" + str(folder),
                str(folder / "native.c"),
                str(folder / "client.c"),
                "-o",
                str(folder / "routing"),
            ]
            if platform == "vial":
                command.append("-DTEST_VIAL")
            if implementation.exists():
                command += [
                    str(implementation),
                    "-Wl,--wrap=rgb_matrix_set_color",
                ]
            compiled = subprocess.run(
                command, capture_output=True, text=True, check=False
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run(
                [str(folder / "routing")],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_vial_bluetooth_indicators_stay_on_their_own_half(self) -> None:
        self.check_platform("vial", "vial-qmk")

    def test_via_rgb_routing_is_preserved(self) -> None:
        self.check_platform("via", "qmk")

# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute native reset behavior without replacing its reset dispatch logic."""

import sys
from pathlib import Path

from test_nkro_boot import NkroBootTests


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    source = (root / "dependencies/zmk/app/src/behaviors/behavior_reset.c").read_text()
    configuration = source[
        source.index("struct behavior_reset_config {") : source.index(
            "static int on_keymap_binding_pressed"
        )
    ]
    fixture = [
        "#define IS_ENABLED(value) (value)",
        "#include <zephyr/device.h>",
        "#include <zephyr/sys/reboot.h>",
        "#include <zmk/behavior.h>",
        "#define CONFIG_RETENTION_BOOT_MODE 0",
        "#define LOG_ERR(...)",
        configuration,
        NkroBootTests.function(
            source,
            "static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,\n"
            "                                     struct zmk_behavior_binding_event event)",
        ),
        "static struct behavior_reset_config config;",
        "static const struct device dev = {.config = &config};",
        "const struct device *zmk_behavior_get_binding(const char *name) {",
        "(void)name; return &dev; }",
        "int native_reset_press(int type) {",
        "config.type = type;",
        'struct zmk_behavior_binding binding = {.behavior_dev = "native"};',
        "return on_keymap_binding_pressed(&binding, (struct zmk_behavior_binding_event){0});",
        "}",
    ]
    Path(sys.argv[1]).write_text("\n".join(fixture))


if __name__ == "__main__":
    main()

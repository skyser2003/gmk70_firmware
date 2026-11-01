"""Generate GMK70 native matrix pins from the authoritative QMK keyboard JSON."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import cast


def _pins(value: object, count: int, *, nullable: bool = False) -> list[str]:
    if not isinstance(value, list) or len(value) != count:
        raise ValueError(f"expected {count} matrix pins")
    result: list[str] = []
    for item in value:
        if item is None and nullable:
            result.append("{255, 0}")
        elif isinstance(item, str) and re.fullmatch(r"[A-D](?:[0-9]|1[0-5])", item):
            result.append(f"{{{ord(item[0]) - ord('A')}, {int(item[1:])}}}")
        else:
            raise ValueError(f"invalid pin: {item!r}")
    return result


def _mapping(value: object) -> dict[str, object]:
    if not isinstance(value, dict):
        raise ValueError("expected keyboard object")
    return cast(dict[str, object], value)


def generate(source: Path) -> str:
    keyboard = _mapping(json.loads(source.read_text()))
    if keyboard.get("diode_direction") != "ROW2COL" or keyboard.get("debounce") != 5:
        raise ValueError("native scanner requires ROW2COL and debounce=5")
    left = _mapping(keyboard.get("matrix_pins"))
    split = _mapping(keyboard.get("split"))
    right = _mapping(_mapping(split.get("matrix_pins")).get("right"))
    handedness = _mapping(split.get("handedness"))
    role = _pins([handedness.get("pin")], 1)[0]
    parts = [
        "/* Generated from firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json. */",
        "/* SPDX-License-Identifier: GPL-2.0-or-later */",
        "#pragma once",
        '#include "wired_pin.h"',
        f"static const struct gmk_wired_pin gmk_role_pin = {role};",
    ]
    rotary = _mapping(keyboard.get("encoder")).get("rotary")
    if not isinstance(rotary, list) or len(rotary) != 1:
        raise ValueError("native encoder requires exactly one left rotary")
    encoder = _mapping(rotary[0])
    if encoder.get("pin_a") == encoder.get("pin_b"):
        raise ValueError("encoder pins must be distinct")
    for axis in ("a", "b"):
        pin = _pins([encoder.get(f"pin_{axis}")], 1)[0]
        parts.append(f"static const struct gmk_wired_pin gmk_encoder_{axis} = {pin};")
    resolution = encoder.get("resolution", 4)
    if type(resolution) is not int or not 1 <= resolution <= 32:
        raise ValueError("encoder resolution must be 1..32")
    parts.append(f"#define GMK_ENCODER_RESOLUTION {resolution}")
    for side, matrix in (("left", left), ("right", right)):
        for axis, count in (("rows", 5), ("cols", 9)):
            entries = ", ".join(_pins(matrix.get(axis), count, nullable=axis == "cols"))
            parts.append(
                f"static const struct gmk_wired_pin gmk_{side}_{axis}[{count}] = {{{entries}}};"
            )
    return "\n".join(parts) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keyboard", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    header = generate(args.keyboard)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(header)


if __name__ == "__main__":
    main()

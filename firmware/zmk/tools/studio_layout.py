"""Generate Studio physical geometry from VIA in authoritative matrix order."""

from __future__ import annotations

import argparse
from decimal import Decimal
import json
from pathlib import Path
import re
from typing import cast


def mapping(value: object) -> dict[str, object]:
    if not isinstance(value, dict):
        raise ValueError("expected an object")
    return cast(dict[str, object], value)


def sequence(value: object) -> list[object]:
    if not isinstance(value, list):
        raise ValueError("expected an array")
    return cast(list[object], value)


def centi(value: object, *, positive: bool = False) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, Decimal)):
        raise ValueError("expected numeric key geometry")
    scaled = Decimal(value) * 100
    if not scaled.is_finite() or scaled != int(scaled):
        raise ValueError("key geometry must use whole centi-units")
    result = int(scaled)
    if abs(result) > 32767 or (positive and result <= 0):
        raise ValueError("key geometry is out of bounds")
    return result


def generate(keyboard: Path, via: Path) -> str:
    board = mapping(json.loads(keyboard.read_text()))
    layout = mapping(mapping(board["layouts"])["LAYOUT"])["layout"]
    order: list[tuple[int, int]] = []
    for entry in sequence(layout):
        pair = sequence(mapping(entry)["matrix"])
        if len(pair) != 2 or any(type(n) is not int for n in pair):
            raise ValueError("invalid matrix position")
        row, col = cast(tuple[int, int], tuple(pair))
        if not (0 <= row < 10 and 0 <= col < 9) or (row, col) in order:
            raise ValueError("duplicate or out-of-range matrix position")
        order.append((row, col))
    data = mapping(json.loads(via.read_text(), parse_float=Decimal))
    rows = sequence(mapping(data["layouts"])["keymap"])
    positions: dict[tuple[int, int], tuple[int, int, int, int]] = {}
    y = 0
    for row_items in rows:
        x, width, height = 0, 100, 100
        for item in sequence(row_items):
            if isinstance(item, dict):
                props = mapping(item)
                if any(
                    props.get(k, 0) for k in ("r", "rx", "ry", "x2", "y2", "w2", "h2")
                ):
                    raise ValueError("unsupported rotated or nonrectangular key")
                x += centi(props.get("x", 0))
                y += centi(props.get("y", 0))
                width = centi(props.get("w", Decimal(width) / 100), positive=True)
                height = centi(props.get("h", Decimal(height) / 100), positive=True)
                continue
            if not isinstance(item, str):
                raise ValueError("expected KLE key label")
            match = re.fullmatch(r"([0-9]+),([0-9]+)", item.split("\n", 1)[0])
            if not match:
                raise ValueError("key lacks matrix position")
            coord = (int(match[1]), int(match[2]))
            if coord in positions or not (0 <= x <= 32767 and 0 <= y <= 32767):
                raise ValueError("duplicate key or out-of-range geometry")
            positions[coord] = (width, height, x, y)
            x += width
            width = height = 100
        y += 100
    if not order or set(order) != positions.keys():
        raise ValueError("VIA and firmware matrix positions do not match")
    lines = [
        "/* Generated from unchanged VIA geometry and maintained matrix order. */",
        "/* SPDX-License-Identifier: GPL-2.0-or-later */",
        "#include <physical_layouts.dtsi>",
        "/ {",
        "    chosen {",
        "        /delete-property/ zmk,matrix-transform;",
        "        zmk,physical-layout = &gmk70_studio_layout;",
        "    };",
        "    gmk70_studio_layout: gmk70_studio_layout {",
        '        compatible = "zmk,physical-layout";',
        '        display-name = "GMK70";',
        "        transform = <&default_transform>;",
        "        kscan = <&kscan0>;",
        "        keys =",
    ]
    for i, coord in enumerate(order):
        w, h, x, y = positions[coord]
        end = ";" if i == len(order) - 1 else ","
        lines.append(f"            <&key_physical_attrs {w} {h} {x} {y} 0 0 0>{end}")
    return "\n".join([*lines, "    };", "};", ""])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keyboard", required=True, type=Path)
    parser.add_argument("--via", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = generate(args.keyboard, args.via)
    except (ValueError, KeyError) as error:
        parser.exit(2, f"Invalid Studio layout: {error}\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)


if __name__ == "__main__":
    main()

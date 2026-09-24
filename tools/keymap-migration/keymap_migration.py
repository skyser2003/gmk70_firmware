"""Offline conversion of the earlier GMK70 port's keycodes to stock V13 slots."""

import argparse
import json
import re
from pathlib import Path
from typing import ClassVar

type JsonValue = (
    str | int | float | bool | None | list[JsonValue] | dict[str, JsonValue]
)


class LegacyKeymapMigration:
    # Only use on exports made with this project's earlier compact enumeration.
    _mapping: ClassVar[dict[int, int]] = {
        0x7E03: 0x7E05,  # 2.4 GHz
        0x7E04: 0x7E09,  # lighting mode
        0x7E05: 0x7E1B,  # battery level
        0x7E06: 0x7E1A,  # function flip
        0x7E08: 0x7E1C,  # undo
        0x7E09: 0x7E1D,  # cut
        0x7E0A: 0x7E1E,  # copy
        0x7E0B: 0x7E1F,  # paste
        0x7E0C: 0x0000,  # formerly reserved / no handler
    }

    @classmethod
    def _keycode(cls, value: JsonValue) -> JsonValue:
        if isinstance(value, bool):
            raise TypeError("Boolean is not a keycode")
        if isinstance(value, int):
            if not 0 <= value <= 0xFFFF:
                raise ValueError("Keycode outside uint16 range")
            return cls._mapping.get(value, value)
        if not isinstance(value, str):
            raise TypeError("Keycodes must be integers or strings")
        if re.fullmatch(r"0[xX][0-9a-fA-F]{1,4}", value):
            number = int(value, 16)
            return f"0x{cls._mapping[number]:04X}" if number in cls._mapping else value
        match = re.fullmatch(r"(USER|QK_KB_)([0-9]+)", value)
        if match:
            slot = int(match[2])
            if slot > 63:
                raise ValueError("Custom key slot outside QMK keyboard range")
            number = 0x7E00 + slot
            if number in cls._mapping:
                mapped = cls._mapping[number]
                if mapped == 0:
                    return "KC_NO"
                return (
                    f"USER{mapped - 0x7E00:02d}"
                    if match[1] == "USER"
                    else f"QK_KB_{mapped - 0x7E00}"
                )
        return value

    @classmethod
    def _keymap(cls, value: JsonValue) -> JsonValue:
        if isinstance(value, list):
            return [cls._keymap(item) for item in value]
        return cls._keycode(value)

    @classmethod
    def convert(cls, document: JsonValue) -> dict[str, JsonValue]:
        if not isinstance(document, dict):
            raise TypeError("Expected a VIA export or raw matrix backup")
        if document.get("keycode_abi") == "stock-v13":
            raise ValueError("This backup is already converted")
        fields = [
            name
            for name in ("layers", "encoders", "matrix_keycodes")
            if name in document
        ]
        if not ("layers" in fields or "matrix_keycodes" in fields):
            raise ValueError(
                "No saved keymap found; a keyboard definition is not an export"
            )
        result = dict(document)
        for field in fields:
            if not isinstance(document[field], list):
                raise TypeError(f"{field} must be an array")
            result[field] = cls._keymap(document[field])
        if "matrix_keycodes" in result:
            result["keycode_abi"] = "stock-v13"
            # Historical recovery actions describe the old dump, not this keymap.
            result.pop("planned_changes", None)
        return result

    @classmethod
    def convert_file(cls, source: Path, destination: Path) -> None:
        if source.resolve() == destination.resolve():
            raise ValueError(
                "The input must remain unchanged; choose another output path"
            )
        document: JsonValue = json.loads(source.read_text(encoding="utf-8-sig"))
        converted = cls.convert(document)
        with destination.open("x", encoding="utf-8", newline="\n") as output:
            output.write(json.dumps(converted, indent=2, ensure_ascii=False) + "\n")

    @classmethod
    def main(cls) -> None:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument(
            "--from", dest="source_abi", choices=["legacy-port-v1"], required=True
        )
        parser.add_argument("input", type=Path)
        parser.add_argument("output", type=Path)
        args = parser.parse_args()
        cls.convert_file(args.input, args.output)
        print(f"Converted file: {args.output}. No device was accessed.")


if __name__ == "__main__":
    LegacyKeymapMigration.main()

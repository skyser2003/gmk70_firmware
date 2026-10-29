"""Offline Vial export conversion; symbolic output requires Studio ID resolution.

Unresolved bindings are null and block readiness. No USB or firmware-writing
dependencies are imported. Source mappings and macro payloads are never logged.
"""

import argparse
import hashlib
import json
import os
import re
from collections import Counter
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import ClassVar

from keymap_migration import JsonValue


class IssueCategory(StrEnum):
    UNKNOWN = "unknown-keycode"
    UNSUPPORTED = "unsupported-keycode"
    WIRELESS = "wireless"
    LIGHTING = "logo-lighting"
    SLEEP = "board-sleep"
    FUNCTION_FLIP = "function-flip"
    RESERVED = "reserved-board-action"
    GUI_LOCK = "gui-lock"
    NKRO = "nkro-mode"
    SETTINGS_RESET = "qmk-eeprom-reset"
    LAYER_RANGE = "layer-out-of-range"
    LAYER_SEMANTICS = "layer-semantics"
    MATRIX = "unmapped-matrix-cell"
    MACROS = "macros"
    ENCODERS = "encoders"
    TAP_DANCE = "tap-dance"
    COMBOS = "combos"
    OVERRIDES = "key-overrides"
    ALT_REPEAT = "alternate-repeat"
    LAYOUT_OPTIONS = "layout-options"
    UNKNOWN_FIELD = "unknown-export-field"


class UnsupportedPolicy(StrEnum):
    REJECT = "reject"
    UNASSIGNED = "unassigned"


class IssueDisposition(StrEnum):
    UNRESOLVED = "unresolved"
    UNASSIGNED = "unassigned"


class UnsupportedKeycode(ValueError):
    def __init__(self, category: IssueCategory) -> None:
        self.category = category
        # Deliberately omit the source token, which may contain personal content.
        super().__init__(category.value)


@dataclass(frozen=True, slots=True)
class Binding:
    behavior: str
    parameters: tuple[int, ...]

    def document(self) -> dict[str, JsonValue]:
        result: dict[str, JsonValue] = {"behavior": self.behavior}
        for index, parameter in enumerate(self.parameters, start=1):
            result[f"param{index}"] = parameter
        return result


@dataclass(frozen=True, slots=True)
class Position:
    row: int
    column: int


@dataclass(frozen=True, slots=True)
class StudioLayer:
    name: str
    bindings: tuple[Binding | None, ...]

    def document(self) -> dict[str, JsonValue]:
        return {
            "name": self.name,
            "bindings": [
                binding.document() if binding is not None else None
                for binding in self.bindings
            ],
        }


@dataclass(frozen=True, slots=True)
class ConversionIssue:
    category: IssueCategory
    location: str
    disposition: IssueDisposition = IssueDisposition.UNRESOLVED

    def document(self) -> dict[str, JsonValue]:
        return {
            "category": self.category.value,
            "location": self.location,
            "disposition": self.disposition.value,
        }


@dataclass(frozen=True, slots=True)
class ExcludedData:
    category: str
    count: int


@dataclass(frozen=True, slots=True)
class Conversion:
    layers: tuple[StudioLayer, ...]
    issues: tuple[ConversionIssue, ...]
    excluded: tuple[ExcludedData, ...]
    positions: tuple[Position, ...]
    layout_options: int
    policy: UnsupportedPolicy

    @property
    def ready(self) -> bool:
        return all(
            issue.disposition != IssueDisposition.UNRESOLVED for issue in self.issues
        )

    def issue_counts(self) -> dict[str, int]:
        return dict(
            sorted(Counter(issue.category.value for issue in self.issues).items())
        )

    def summary(self) -> str:
        unassigned = sum(
            issue.disposition == IssueDisposition.UNASSIGNED for issue in self.issues
        )
        translated = (
            sum(
                binding is not None
                for layer in self.layers
                for binding in layer.bindings
            )
            - unassigned
        )
        counts = ", ".join(
            f"{name}={count}" for name, count in self.issue_counts().items()
        )
        return (
            f"{len(self.layers)} layers, {len(self.positions)} positions each; "
            f"{translated} translated bindings; {unassigned} explicitly unassigned; "
            f"unsupported categories: {counts or 'none'}. "
            f"Ready for behavior-ID resolution: {self.ready}. No device was accessed."
        )

    def document(self, source_sha256: str) -> dict[str, JsonValue]:
        return {
            "format": "gmk70-zmk-studio-symbolic-v1",
            "source_sha256": source_sha256,
            "ready_for_behavior_id_resolution": self.ready,
            "unsupported_policy": self.policy.value,
            "layout_options": self.layout_options,
            "positions": [
                [position.row, position.column] for position in self.positions
            ],
            "layers": [layer.document() for layer in self.layers],
            "issues": [issue.document() for issue in self.issues],
            "excluded": [
                {"category": item.category, "count": item.count}
                for item in self.excluded
            ],
            "notes": [
                "Symbolic behavior names require resolution against the target firmware.",
                "Layer indices require resolution against the target Studio layer IDs.",
                "Null bindings are unresolved and must never be silently replaced.",
                "Vial tuning settings are not ZMK Studio bindings and are not imported.",
                "This file is a migration plan, not a ZMK Studio native import file.",
            ],
        }


class JsonSchema:
    @staticmethod
    def mapping(value: JsonValue, context: str) -> dict[str, JsonValue]:
        if not isinstance(value, dict):
            raise TypeError(f"{context} must be a JSON mapping")
        return value

    @staticmethod
    def sequence(value: JsonValue, context: str) -> list[JsonValue]:
        if not isinstance(value, list):
            raise TypeError(f"{context} must be an array")
        return value

    @staticmethod
    def integer(value: JsonValue, context: str) -> int:
        if isinstance(value, bool) or not isinstance(value, int):
            raise TypeError(f"{context} must be an integer")
        return value


class KeycodeCatalog:
    """Read aliases and HID translations from the pinned Vial/QMK sources."""

    _aliases: ClassVar[dict[str, str]] = {
        "KC_LCTRL": "KC_LCTL",
        "KC_RCTRL": "KC_RCTL",
        "KC_LSHIFT": "KC_LSFT",
        "KC_RSHIFT": "KC_RSFT",
        "KC_BSPACE": "KC_BSPC",
        "KC_LBRACKET": "KC_LBRC",
        "KC_RBRACKET": "KC_RBRC",
        "KC_BSLASH": "KC_BSLS",
        "KC_SCOLON": "KC_SCLN",
        "KC_CAPSLOCK": "KC_CAPS",
        "KC_PSCREEN": "KC_PSCR",
        "KC_PGDOWN": "KC_PGDN",
        "KC_NUMLOCK": "KC_NUM",
        "KC_SCROLLLOCK": "KC_SCRL",
        "KC_NONUS_BSLASH": "KC_NUBS",
        "KC_NONUS_HASH": "KC_NUHS",
        "RGB_TOG": "UG_TOGG",
        "RGB_MOD": "UG_NEXT",
        "RGB_RMOD": "UG_PREV",
        "RGB_HUI": "UG_HUEU",
        "RGB_HUD": "UG_HUED",
        "RGB_SAI": "UG_SATU",
        "RGB_SAD": "UG_SATD",
        "RGB_VAI": "UG_VALU",
        "RGB_VAD": "UG_VALD",
        "RGB_SPI": "UG_SPDU",
        "RGB_SPD": "UG_SPDD",
    }
    _modifiers: ClassVar[dict[str, int]] = {
        "LCTL": 0x01,
        "C": 0x01,
        "LSFT": 0x02,
        "S": 0x02,
        "LALT": 0x04,
        "A": 0x04,
        "LGUI": 0x08,
        "G": 0x08,
        "LCMD": 0x08,
        "LWIN": 0x08,
        "RCTL": 0x11,
        "RSFT": 0x12,
        "RALT": 0x14,
        "ALGR": 0x14,
        "RGUI": 0x18,
        "RCMD": 0x18,
        "RWIN": 0x18,
        "HYPR": 0x0F,
        "MEH": 0x07,
        "C_S": 0x03,
        "LCA": 0x05,
        "LCAG": 0x0D,
        "LCG": 0x09,
        "LAG": 0x0C,
        "LSA": 0x06,
        "SGUI": 0x0A,
        "RCG": 0x19,
    }
    _layer_bases: ClassVar[dict[str, int]] = {
        "TO": 0x5200,
        "MO": 0x5220,
        "DF": 0x5240,
        "TG": 0x5260,
        "OSL": 0x5280,
        "TT": 0x52C0,
        "PDF": 0x52E0,
    }

    def __init__(self, repository: Path) -> None:
        qmk = repository / "dependencies/vial-qmk"
        self._codes: dict[str, int] = {}
        self._expressions: dict[str, str] = {}
        self._hid: dict[int, int] = {}
        for header in (
            qmk / "quantum/keycodes.h",
            repository / "firmware/qmk-common/keyboards/zuoya/gmk70/stock_keycodes.h",
            repository / "firmware/qmk-vial/compat/manual_keycodes.h",
        ):
            self._read_enum(header.read_text(encoding="utf-8"))
        us_aliases = (qmk / "quantum/keymap_extras/keymap_us.h").read_text(
            encoding="utf-8"
        )
        for match in re.finditer(
            r"^#define\s+(KC_\w+)\s+([^\n/]+)", us_aliases, re.MULTILINE
        ):
            self._expressions[match[1]] = match[2].strip()
        report = (qmk / "tmk_core/protocol/report.h").read_text(encoding="utf-8")
        self._read_enum(report)
        for section, page in (("SYSTEM", 1), ("CONSUMER", 12)):
            section_match = re.search(
                rf"KEYCODE2{section}\(uint8_t key\)\s*\{{(.*?)\n\}}", report, re.DOTALL
            )
            if section_match is None:
                raise ValueError("Pinned QMK HID translation table was not found")
            for case in re.finditer(
                r"case (KC_\w+):\s+return (\w+);", section_match[1]
            ):
                self._hid[self._codes[case[1]]] = (page << 16) | self._codes[case[2]]

    def _read_enum(self, text: str) -> None:
        for match in re.finditer(
            r"^\s*(\w+)\s*=\s*(0x[0-9a-fA-F]+|[A-Z_][A-Z_0-9]*)\s*[,\n]",
            text,
            re.MULTILINE,
        ):
            value = match[2]
            if value.startswith("0x"):
                self._codes[match[1]] = int(value, 16)
            elif value in self._codes:
                self._codes[match[1]] = self._codes[value]

    def decode(self, value: JsonValue, depth: int = 0) -> int:
        if depth > 16:
            raise UnsupportedKeycode(IssueCategory.UNKNOWN)
        if isinstance(value, bool) or not isinstance(value, (int, str)):
            raise TypeError("A keycode must be a uint16 or a symbolic string")
        if isinstance(value, int):
            if not 0 <= value <= 0xFFFF:
                raise ValueError("A numeric keycode must fit uint16")
            return value
        token = value.strip()
        if re.fullmatch(r"0[xX][0-9a-fA-F]{1,4}", token):
            return int(token, 16)
        if token in self._codes:
            return self._codes[token]
        if token in self._aliases:
            return self.decode(self._aliases[token], depth + 1)
        if token in self._expressions:
            return self.decode(self._expressions[token], depth + 1)
        user = re.fullmatch(r"USER([0-9]+)", token)
        if user is not None:
            index = int(user[1])
            if index > 63:
                raise ValueError("A Vial USER slot must be between 0 and 63")
            return 0x7E00 + index
        call = re.fullmatch(r"([A-Z_]+)\((.*)\)", token)
        if call is not None:
            name, argument = call[1], call[2].strip()
            if name in self._layer_bases and argument.isdecimal():
                layer = int(argument)
                if not 0 <= layer <= 31:
                    raise UnsupportedKeycode(IssueCategory.LAYER_RANGE)
                return self._layer_bases[name] + layer
            if name in self._modifiers:
                inner = self.decode(argument, depth + 1)
                if inner >= 0x2000:
                    raise UnsupportedKeycode(IssueCategory.UNSUPPORTED)
                return (self._modifiers[name] << 8) | inner
        raise UnsupportedKeycode(IssueCategory.UNKNOWN)

    def hid_usage(self, code: int) -> int:
        if code in self._hid:
            return self._hid[code]
        if 4 <= code <= 0xA4 or 0xE0 <= code <= 0xE7:
            return 0x70000 | code
        raise UnsupportedKeycode(IssueCategory.UNSUPPORTED)


class VialToZmk:
    _rgb: ClassVar[dict[int, int]] = {
        0x7820: 0,
        0x7821: 11,
        0x7822: 12,
        0x7823: 3,
        0x7824: 4,
        0x7825: 5,
        0x7826: 6,
        0x7827: 7,
        0x7828: 8,
        0x7829: 9,
        0x782A: 10,
        0x7840: 1,
        0x7841: 2,
        0x7842: 0,
        0x7843: 11,
        0x7844: 12,
        0x7845: 3,
        0x7846: 4,
        0x7847: 5,
        0x7848: 6,
        0x7849: 7,
        0x784A: 8,
        0x784B: 9,
        0x784C: 10,
        0x7E25: 0,
        0x7E26: 11,
        0x7E27: 7,
        0x7E28: 8,
        0x7E29: 9,
        0x7E2A: 10,
        0x7E2B: 3,
        0x7E2C: 4,
        0x7E2D: 5,
        0x7E2E: 6,
    }
    _known_fields: ClassVar[frozenset[str]] = frozenset(
        {
            "version",
            "uid",
            "layout",
            "encoder_layout",
            "layout_options",
            "macro",
            "vial_protocol",
            "via_protocol",
            "tap_dance",
            "combo",
            "key_override",
            "alt_repeat_key",
            "settings",
        }
    )

    def __init__(self, repository: Path) -> None:
        self._catalog = KeycodeCatalog(repository)
        keyboard = (
            repository / "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json"
        )
        document: JsonValue = json.loads(keyboard.read_text(encoding="utf-8"))
        board = JsonSchema.mapping(document, "keyboard")
        layouts = JsonSchema.mapping(board.get("layouts"), "keyboard layouts")
        layout = JsonSchema.mapping(layouts.get("LAYOUT"), "keyboard LAYOUT")
        positions: list[Position] = []
        for entry in JsonSchema.sequence(layout.get("layout"), "keyboard positions"):
            key = JsonSchema.mapping(entry, "keyboard position")
            pair = JsonSchema.sequence(key.get("matrix"), "matrix position")
            if len(pair) != 2:
                raise ValueError("A matrix position requires two coordinates")
            row = JsonSchema.integer(pair[0], "matrix row")
            column = JsonSchema.integer(pair[1], "matrix column")
            position = Position(row, column)
            if not (0 <= row < 10 and 0 <= column < 9) or position in positions:
                raise ValueError(
                    "A board matrix position is duplicate or out of bounds"
                )
            positions.append(position)
        if len(positions) != 72:
            raise ValueError("The maintained GMK70 geometry must have 72 positions")
        self._positions = tuple(positions)

    def translate(self, value: JsonValue, layer_count: int) -> Binding:
        code = self._catalog.decode(value)
        if code in (0, 1):
            return Binding("none" if code == 0 else "trans", ())
        if code <= 0xFF:
            return Binding("kp", (self._catalog.hid_usage(code),))
        if 0x0100 <= code <= 0x1FFF:
            modifiers = (code >> 8) & 0x1F
            mods = (modifiers & 0x0F) << (4 if modifiers & 0x10 else 0)
            return Binding("kp", ((mods << 24) | self._catalog.hid_usage(code & 0xFF),))
        if 0x5200 <= code <= 0x52FF:
            base, layer = code & 0xFFE0, code & 0x1F
            if base not in (0x5200, 0x5220, 0x5260):
                # DF/PDF change a default layer; &to only changes active layers.
                raise UnsupportedKeycode(IssueCategory.LAYER_SEMANTICS)
            if layer >= layer_count:
                raise UnsupportedKeycode(IssueCategory.LAYER_RANGE)
            return Binding({0x5200: "to", 0x5220: "mo", 0x5260: "tog"}[base], (layer,))
        if code in self._rgb:
            return Binding("rgb_ug", (self._rgb[code], 0))
        if code in (0x7E07, 0x7E1B):
            return Binding("gmk_battery", (1 if code == 0x7E07 else 0,))
        if code in (0x7E22, 0x7E23):
            return self.translate(0x5200 + (2 if code == 0x7E23 else 0), layer_count)
        if 0x7E1C <= code <= 0x7E21:
            shortcuts = (0x011D, 0x011B, 0x0106, 0x0119, 0x082B, 0x0808)
            return self.translate(shortcuts[code - 0x7E1C], layer_count)
        if 0x7E00 <= code <= 0x7E06:
            raise UnsupportedKeycode(IssueCategory.WIRELESS)
        if code == 0x7E09:
            raise UnsupportedKeycode(IssueCategory.LIGHTING)
        if code in (0x7E11, 0x7E12, 0x7E13):
            raise UnsupportedKeycode(IssueCategory.SLEEP)
        if code == 0x7E1A:
            raise UnsupportedKeycode(IssueCategory.FUNCTION_FLIP)
        if code in (0x7009, 0x700A, 0x700B, 0x7E24):
            raise UnsupportedKeycode(IssueCategory.GUI_LOCK)
        if code in (0x7011, 0x7012, 0x7013):
            raise UnsupportedKeycode(IssueCategory.NKRO)
        if code == 0x7C03:
            raise UnsupportedKeycode(IssueCategory.SETTINGS_RESET)
        if 0x7E00 <= code <= 0x7E3F:
            raise UnsupportedKeycode(IssueCategory.RESERVED)
        raise UnsupportedKeycode(IssueCategory.UNSUPPORTED)

    def _empty(self, value: JsonValue) -> bool:
        try:
            return self._catalog.decode(value) in (0, 1)
        except UnsupportedKeycode:
            return False

    def _auxiliary(
        self, document: dict[str, JsonValue], layer_count: int
    ) -> tuple[list[ConversionIssue], list[ExcludedData]]:
        issues: list[ConversionIssue] = []
        excluded: list[ExcludedData] = []
        macros = JsonSchema.sequence(document.get("macro", []), "macro")
        for index, macro in enumerate(macros):
            if JsonSchema.sequence(macro, "macro entry"):
                issues.append(ConversionIssue(IssueCategory.MACROS, f"macro[{index}]"))
        excluded.append(
            ExcludedData("empty-macro-slots", sum(not macro for macro in macros))
        )
        encoders = JsonSchema.sequence(
            document.get("encoder_layout", []), "encoder_layout"
        )
        if encoders and len(encoders) != layer_count:
            raise ValueError("Encoder and keymap layer counts must match")
        for layer, entries in enumerate(encoders):
            for encoder, entry in enumerate(
                JsonSchema.sequence(entries, "encoder layer")
            ):
                directions = JsonSchema.sequence(entry, "encoder entry")
                if len(directions) != 2:
                    raise ValueError("An encoder entry requires two directions")
                for direction, value in enumerate(directions):
                    if not self._empty(value):
                        issues.append(
                            ConversionIssue(
                                IssueCategory.ENCODERS,
                                f"encoder_layout[{layer}][{encoder}][{direction}]",
                            )
                        )
        for field, category, keys in (
            ("tap_dance", IssueCategory.TAP_DANCE, ()),
            ("combo", IssueCategory.COMBOS, ()),
            ("key_override", IssueCategory.OVERRIDES, ("trigger", "replacement")),
            ("alt_repeat_key", IssueCategory.ALT_REPEAT, ("keycode", "alt_keycode")),
        ):
            entries = JsonSchema.sequence(document.get(field, []), field)
            inactive = 0
            for index, entry in enumerate(entries):
                if keys:
                    rule = JsonSchema.mapping(entry, field)
                    values = [rule.get(key) for key in keys]
                else:
                    values = JsonSchema.sequence(entry, field)
                    if len(values) != 5:
                        raise ValueError(f"{field} entries require five values")
                    values = values[:4] if field == "tap_dance" else values
                if all(self._empty(value) for value in values):
                    inactive += 1
                else:
                    issues.append(ConversionIssue(category, f"{field}[{index}]"))
            excluded.append(ExcludedData(f"inactive-{category.value}-slots", inactive))
        settings = JsonSchema.mapping(document.get("settings", {}), "settings")
        excluded.append(
            ExcludedData("vial-tuning-settings-not-imported", len(settings))
        )
        for field in document.keys() - self._known_fields:
            issues.append(ConversionIssue(IssueCategory.UNKNOWN_FIELD, field))
        return issues, excluded

    def convert(
        self, document: JsonValue, policy: UnsupportedPolicy = UnsupportedPolicy.REJECT
    ) -> Conversion:
        source = JsonSchema.mapping(document, "Vial export")
        for field, expected in (
            ("version", 1),
            ("vial_protocol", 6),
            ("via_protocol", 9),
        ):
            if JsonSchema.integer(source.get(field), field) != expected:
                raise ValueError(f"Unsupported Vial {field}; expected {expected}")
        options = JsonSchema.integer(source.get("layout_options", -1), "layout_options")
        layout = JsonSchema.sequence(source.get("layout"), "layout")
        if not 1 <= len(layout) <= 16:
            raise ValueError(
                "The Studio layout requires between one and sixteen layers"
            )
        issues, excluded = self._auxiliary(source, len(layout))
        if options not in (-1, 0):
            issues.append(
                ConversionIssue(IssueCategory.LAYOUT_OPTIONS, "layout_options")
            )
        layers: list[StudioLayer] = []
        for layer_index, layer_value in enumerate(layout):
            rows = JsonSchema.sequence(layer_value, "layer matrix")
            if len(rows) != 10:
                raise ValueError("A GMK70 layer matrix requires ten rows")
            cells: dict[Position, JsonValue] = {}
            for row_index, row_value in enumerate(rows):
                row = JsonSchema.sequence(row_value, "matrix row")
                if len(row) != 9:
                    raise ValueError("A GMK70 matrix row requires nine columns")
                for column, value in enumerate(row):
                    position = Position(row_index, column)
                    cells[position] = value
                    if (
                        position not in self._positions
                        and isinstance(value, int)
                        and value == -1
                    ):
                        # Native Vial uses -1 for nonexistent matrix positions.
                        continue
                    if position not in self._positions and not self._empty(value):
                        issues.append(
                            ConversionIssue(
                                IssueCategory.MATRIX,
                                f"layout[{layer_index}][{row_index}][{column}]",
                            )
                        )
            bindings: list[Binding | None] = []
            for index, position in enumerate(self._positions):
                binding: Binding | None
                try:
                    binding = self.translate(cells[position], len(layout))
                except UnsupportedKeycode as error:
                    disposition = (
                        IssueDisposition.UNASSIGNED
                        if policy == UnsupportedPolicy.UNASSIGNED
                        else IssueDisposition.UNRESOLVED
                    )
                    issues.append(
                        ConversionIssue(
                            error.category,
                            f"layers[{layer_index}].bindings[{index}]",
                            disposition,
                        )
                    )
                    binding = (
                        Binding("none", (0, 0))
                        if policy == UnsupportedPolicy.UNASSIGNED
                        else None
                    )
                bindings.append(binding)
            layers.append(StudioLayer(f"Layer {layer_index}", tuple(bindings)))
        return Conversion(
            tuple(layers),
            tuple(issues),
            tuple(excluded),
            self._positions,
            options,
            policy,
        )

    def convert_file(
        self,
        source: Path,
        destination: Path,
        policy: UnsupportedPolicy = UnsupportedPolicy.REJECT,
    ) -> Conversion:
        if source.resolve() == destination.resolve():
            raise ValueError(
                "The source must remain unchanged; choose another output path"
            )
        if destination.exists():
            raise FileExistsError("The output already exists")
        raw = source.read_bytes()
        document: JsonValue = json.loads(raw.decode("utf-8-sig"))
        converted = self.convert(document, policy)
        serialized = (
            json.dumps(converted.document(hashlib.sha256(raw).hexdigest()), indent=2)
            + "\n"
        )
        descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
            output.write(serialized)
        return converted

    @classmethod
    def main(cls) -> None:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("input", type=Path)
        parser.add_argument("output", type=Path)
        parser.add_argument(
            "--unsupported",
            type=UnsupportedPolicy,
            choices=tuple(UnsupportedPolicy),
            default=UnsupportedPolicy.REJECT,
            help="Explicitly leave unsupported physical bindings unassigned; default rejects them",
        )
        parser.add_argument(
            "--repository", type=Path, default=Path(__file__).resolve().parents[2]
        )
        arguments = parser.parse_args(
            namespace=CliArguments(repository=Path(__file__).resolve().parents[2])
        )
        try:
            converted = cls(arguments.repository).convert_file(
                arguments.input, arguments.output, arguments.unsupported
            )
        except (ValueError, TypeError, OSError) as error:
            # JSON parser errors can echo content; log only the error category.
            parser.exit(2, f"Offline migration failed: {type(error).__name__}.\n")
        print(converted.summary())
        if not converted.ready:
            parser.exit(2)


@dataclass
class CliArguments:
    input: Path = Path()
    output: Path = Path()
    repository: Path = Path()
    unsupported: UnsupportedPolicy = UnsupportedPolicy.REJECT


if __name__ == "__main__":
    VialToZmk.main()

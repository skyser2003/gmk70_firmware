"""Explicit Studio RPC snapshot/apply operations, separate from builds and tests.

Wire fields follow the pinned zmk-studio-messages proto files; framing follows
upstream msg_framing.c. The codec has no generated source or device dependency.
Personal bindings are written only to exclusive, mode-0600 private JSON files.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import time
from collections import deque
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import TYPE_CHECKING, ClassVar, Protocol

if TYPE_CHECKING:
    from serial import Serial

type JsonValue = (
    str | int | float | bool | None | list[JsonValue] | dict[str, JsonValue]
)


class ProtocolError(RuntimeError):
    """Malformed, unexpected, rejected, or uncorrelated Studio data."""


class StudioApplyError(RuntimeError):
    def __init__(self, rollback_succeeded: bool) -> None:
        self.rollback_succeeded = rollback_succeeded
        super().__init__(
            "Studio apply failed; fresh original map restored and verified"
            if rollback_succeeded
            else "Studio apply failed and restoration could not be verified"
        )


class ByteTransport(Protocol):
    """Reads and writes must return within a finite timeout; no reconnection."""

    def read(self, size: int) -> bytes: ...
    def write(self, data: bytes) -> int: ...


class SerialByteTransport:
    def __init__(self, port: str) -> None:
        from serial import Serial

        self._serial: Serial = Serial(
            port=port, baudrate=115200, timeout=0.1, write_timeout=0.5
        )

    def read(self, size: int) -> bytes:
        return self._serial.read(size)

    def write(self, data: bytes) -> int:
        written = self._serial.write(data)
        if written is None:
            raise ProtocolError("Serial write returned no byte count")
        return written

    def close(self) -> None:
        self._serial.close()


class WireType(IntEnum):
    UINT = 0
    FIXED64 = 1
    BLOB = 2
    FIXED32 = 5


@dataclass(frozen=True, slots=True)
class WireField:
    number: int
    wire_type: WireType
    value: int | bytes


@dataclass(frozen=True, slots=True)
class WireMessage:
    fields: tuple[WireField, ...]

    def has(self, number: int) -> bool:
        return any(field.number == number for field in self.fields)

    def which(self, numbers: tuple[int, ...]) -> int:
        matches = tuple(
            field.number for field in self.fields if field.number in numbers
        )
        if len(matches) != 1:
            raise ProtocolError("Missing or conflicting protobuf oneof")
        return matches[0]

    def one(self, number: int) -> WireField | None:
        matching = tuple(field for field in self.fields if field.number == number)
        if len(matching) > 1:
            raise ProtocolError("Duplicate singular protobuf field")
        return matching[0] if matching else None

    def uint(self, number: int, *, required: bool = False) -> int:
        field = self.one(number)
        if field is None:
            if required:
                raise ProtocolError("Missing protobuf scalar field")
            return 0
        if field.wire_type != WireType.UINT or not isinstance(field.value, int):
            raise ProtocolError("Wrong protobuf scalar wire type")
        if field.value > 0xFFFFFFFF:
            raise ProtocolError("Protobuf uint32 overflow")
        return field.value

    def zigzag(self, number: int) -> int:
        value = self.uint(number)
        return (value >> 1) ^ -(value & 1)

    def signed(self, number: int) -> int:
        field = self.one(number)
        if field is None:
            return 0
        if field.wire_type != WireType.UINT or not isinstance(field.value, int):
            raise ProtocolError("Wrong protobuf int32 wire type")
        value = field.value & 0xFFFFFFFF
        return value - 0x100000000 if value & 0x80000000 else value

    def blob(self, number: int, *, required: bool = True) -> bytes:
        field = self.one(number)
        if field is None:
            if required:
                raise ProtocolError("Missing protobuf message field")
            return b""
        if field.wire_type != WireType.BLOB or not isinstance(field.value, bytes):
            raise ProtocolError("Wrong protobuf message wire type")
        return field.value

    def blobs(self, number: int) -> tuple[bytes, ...]:
        result: list[bytes] = []
        for field in self.fields:
            if field.number == number:
                if field.wire_type != WireType.BLOB or not isinstance(
                    field.value, bytes
                ):
                    raise ProtocolError("Wrong repeated message wire type")
                result.append(field.value)
        return tuple(result)

    def text(self, number: int) -> str:
        try:
            return self.blob(number, required=False).decode("utf-8", errors="strict")
        except UnicodeError as error:
            raise ProtocolError("Invalid UTF-8 in Studio metadata") from error


class Protobuf:
    @staticmethod
    def varint(value: int) -> bytes:
        if not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
            raise ValueError("Protobuf varint is outside uint64")
        result = bytearray()
        while value > 0x7F:
            result.append((value & 0x7F) | 0x80)
            value >>= 7
        result.append(value)
        return bytes(result)

    @staticmethod
    def read_varint(data: bytes, offset: int) -> tuple[int, int]:
        result = 0
        for index in range(10):
            if offset >= len(data):
                raise ProtocolError("Truncated protobuf varint")
            byte = data[offset]
            offset += 1
            if index == 9 and byte > 1:
                raise ProtocolError("Protobuf varint overflow")
            result |= (byte & 0x7F) << (7 * index)
            if byte < 0x80:
                return result, offset
        raise ProtocolError("Protobuf varint is too long")

    @classmethod
    def uint(cls, number: int, value: int) -> bytes:
        if not 1 <= number <= 0x1FFFFFFF or not 0 <= value <= 0xFFFFFFFF:
            raise ValueError("Invalid protobuf uint32 field")
        return cls.varint(number << 3) + cls.varint(value)

    @classmethod
    def blob(cls, number: int, data: bytes) -> bytes:
        if not 1 <= number <= 0x1FFFFFFF or len(data) > 65536:
            raise ValueError("Invalid protobuf length-delimited field")
        return cls.varint((number << 3) | 2) + cls.varint(len(data)) + data

    @classmethod
    def parse(cls, data: bytes) -> WireMessage:
        if len(data) > 65536:
            raise ProtocolError("Studio message exceeds size bound")
        fields: list[WireField] = []
        offset = 0
        while offset < len(data):
            if len(fields) >= 8192:
                raise ProtocolError("Studio message exceeds field-count bound")
            tag, offset = cls.read_varint(data, offset)
            number, wire = tag >> 3, tag & 7
            if not 1 <= number <= 0x1FFFFFFF or wire not in (0, 1, 2, 5):
                raise ProtocolError("Invalid protobuf field tag")
            wire_type = WireType(wire)
            if wire_type == WireType.UINT:
                value, offset = cls.read_varint(data, offset)
                fields.append(WireField(number, wire_type, value))
                continue
            if wire_type == WireType.BLOB:
                size, offset = cls.read_varint(data, offset)
            else:
                size = 8 if wire_type == WireType.FIXED64 else 4
            end = offset + size
            if end > len(data):
                raise ProtocolError("Truncated protobuf bytes field")
            fields.append(WireField(number, wire_type, data[offset:end]))
            offset = end
        return WireMessage(tuple(fields))


class StudioFraming:
    def __init__(self, max_frame: int = 65536) -> None:
        if not 1 <= max_frame <= 65536:
            raise ValueError("Invalid Studio frame bound")
        self._maximum = max_frame
        self._in_frame = False
        self._escaped = False
        self._data = bytearray()

    @staticmethod
    def encode(data: bytes) -> bytes:
        if len(data) > 65536:
            raise ValueError("Studio frame exceeds size bound")
        result = bytearray(b"\xab")
        for value in data:
            if value in (0xAB, 0xAC, 0xAD):
                result.append(0xAC)
            result.append(value)
        result.append(0xAD)
        return bytes(result)

    def feed(self, data: bytes) -> tuple[bytes, ...]:
        frames: list[bytes] = []
        for value in data:
            if not self._in_frame:
                if value == 0xAB:
                    self._in_frame = True
                    self._escaped = False
                    self._data.clear()
                continue
            if self._escaped:
                self._data.append(value)
                self._escaped = False
            elif value == 0xAC:
                self._escaped = True
            elif value == 0xAD:
                frames.append(bytes(self._data))
                self._in_frame = False
            elif value == 0xAB:
                self._in_frame = False
                self._data.clear()
                raise ProtocolError("Unescaped start marker inside Studio frame")
            else:
                self._data.append(value)
            if len(self._data) > self._maximum:
                self._in_frame = False
                self._data.clear()
                raise ProtocolError("Studio frame exceeds size bound")
        return tuple(frames)


@dataclass(frozen=True, slots=True)
class RpcBinding:
    behavior_id: int
    param1: int
    param2: int

    @classmethod
    def decode(cls, data: bytes) -> RpcBinding:
        message = Protobuf.parse(data)
        return cls(message.zigzag(1), message.uint(2), message.uint(3))

    def encode(self) -> bytes:
        if not -0x80000000 <= self.behavior_id <= 0x7FFFFFFF:
            raise ValueError("Behavior ID is outside sint32")
        identifier = (self.behavior_id << 1) ^ (self.behavior_id >> 31)
        return (
            Protobuf.uint(1, identifier)
            + Protobuf.uint(2, self.param1)
            + Protobuf.uint(3, self.param2)
        )

    def document(self) -> dict[str, JsonValue]:
        return {
            "behavior_id": self.behavior_id,
            "param1": self.param1,
            "param2": self.param2,
        }


@dataclass(frozen=True, slots=True)
class RpcLayer:
    layer_id: int
    name: str
    bindings: tuple[RpcBinding, ...]

    def document(self) -> dict[str, JsonValue]:
        return {
            "id": self.layer_id,
            "name": self.name,
            "bindings": [binding.document() for binding in self.bindings],
        }


@dataclass(frozen=True, slots=True)
class RpcKeymap:
    layers: tuple[RpcLayer, ...]
    available_layers: int
    max_layer_name_length: int

    @classmethod
    def decode(cls, data: bytes) -> RpcKeymap:
        message = Protobuf.parse(data)
        layers: list[RpcLayer] = []
        for raw in message.blobs(1):
            layer = Protobuf.parse(raw)
            layers.append(
                RpcLayer(
                    layer.uint(1),
                    layer.text(2),
                    tuple(RpcBinding.decode(binding) for binding in layer.blobs(3)),
                )
            )
        if not 1 <= len(layers) <= 32 or len(
            {layer.layer_id for layer in layers}
        ) != len(layers):
            raise ProtocolError("Invalid or duplicate Studio layer IDs")
        return cls(tuple(layers), message.uint(2), message.uint(3))

    def document(self) -> dict[str, JsonValue]:
        return {
            "layers": [layer.document() for layer in self.layers],
            "available_layers": self.available_layers,
            "max_layer_name_length": self.max_layer_name_length,
        }


class ParameterKind(IntEnum):
    NIL = 2
    CONSTANT = 3
    RANGE = 4
    HID = 5
    LAYER = 6


@dataclass(frozen=True, slots=True)
class ParameterRule:
    kind: ParameterKind
    lower: int = 0
    upper: int = 0

    @classmethod
    def decode(cls, raw: bytes) -> ParameterRule:
        message = Protobuf.parse(raw)
        kinds = tuple(kind for kind in ParameterKind if message.has(kind))
        if len(kinds) != 1:
            raise ProtocolError("Invalid behavior parameter metadata oneof")
        kind = kinds[0]
        if kind == ParameterKind.CONSTANT:
            return cls(kind, message.uint(kind), message.uint(kind))
        payload = Protobuf.parse(message.blob(kind))
        if kind == ParameterKind.RANGE:
            lower, upper = payload.signed(1), payload.signed(2)
            if lower > upper:
                raise ProtocolError("Invalid behavior parameter range")
            return cls(kind, lower, upper)
        if kind == ParameterKind.HID:
            return cls(kind, payload.uint(1), payload.uint(2))
        return cls(kind)

    def allows(self, value: int, layer_ids: frozenset[int]) -> bool:
        if self.kind == ParameterKind.NIL:
            return value == 0
        if self.kind == ParameterKind.CONSTANT:
            return value == self.lower
        if self.kind == ParameterKind.RANGE:
            return self.lower <= value <= self.upper
        if self.kind == ParameterKind.LAYER:
            return value in layer_ids
        page, usage = (value >> 16) & 0xFF, value & 0xFFFF
        return (page == 7 and (1 <= usage <= self.lower or 0xE0 <= usage <= 0xE7)) or (
            page == 12 and usage <= self.upper
        )


@dataclass(frozen=True, slots=True)
class ParameterSet:
    first: tuple[ParameterRule, ...]
    second: tuple[ParameterRule, ...]

    def allows(self, binding: RpcBinding, layer_ids: frozenset[int]) -> bool:
        return all(
            (
                value == 0
                if not rules
                else any(rule.allows(value, layer_ids) for rule in rules)
            )
            for rules, value in (
                (self.first, binding.param1),
                (self.second, binding.param2),
            )
        )


@dataclass(frozen=True, slots=True)
class BehaviorDetails:
    behavior_id: int
    display_name: str
    parameters: tuple[ParameterSet, ...]

    @classmethod
    def decode(cls, data: bytes) -> BehaviorDetails:
        message = Protobuf.parse(data)
        sets: list[ParameterSet] = []
        for raw in message.blobs(3):
            params = Protobuf.parse(raw)
            sets.append(
                ParameterSet(
                    tuple(ParameterRule.decode(rule) for rule in params.blobs(1)),
                    tuple(ParameterRule.decode(rule) for rule in params.blobs(2)),
                )
            )
        return cls(message.uint(1), message.text(2), tuple(sets))

    def allows(self, binding: RpcBinding, layer_ids: frozenset[int]) -> bool:
        if binding.behavior_id != self.behavior_id:
            return False
        if not self.parameters:
            return binding.param1 == 0 and binding.param2 == 0
        return any(
            parameters.allows(binding, layer_ids) for parameters in self.parameters
        )


@dataclass(frozen=True, slots=True)
class MatrixPosition:
    row: int
    column: int


@dataclass(frozen=True, slots=True)
class PhysicalKey:
    width: int
    height: int
    x: int
    y: int
    rotation: int
    rotation_x: int
    rotation_y: int

    @classmethod
    def decode(cls, raw: bytes) -> PhysicalKey:
        message = Protobuf.parse(raw)
        return cls(*(message.zigzag(number) for number in range(1, 8)))


@dataclass(frozen=True, slots=True)
class PhysicalLayout:
    name: str
    keys: tuple[PhysicalKey, ...]


@dataclass(frozen=True, slots=True)
class PhysicalLayouts:
    active_index: int
    layouts: tuple[PhysicalLayout, ...]

    @classmethod
    def decode(cls, data: bytes) -> PhysicalLayouts:
        message = Protobuf.parse(data)
        layouts: list[PhysicalLayout] = []
        for raw in message.blobs(2):
            layout = Protobuf.parse(raw)
            layouts.append(
                PhysicalLayout(
                    layout.text(1),
                    tuple(PhysicalKey.decode(key) for key in layout.blobs(2)),
                )
            )
        selected = message.uint(1)
        if not 0 <= selected < len(layouts):
            raise ProtocolError("Studio active physical layout is missing")
        return cls(selected, tuple(layouts))


class JsonSchema:
    @staticmethod
    def mapping(value: JsonValue) -> dict[str, JsonValue]:
        if not isinstance(value, dict):
            raise TypeError("Expected a JSON mapping")
        return value

    @staticmethod
    def sequence(value: JsonValue) -> list[JsonValue]:
        if not isinstance(value, list):
            raise TypeError("Expected a JSON array")
        return value

    @staticmethod
    def uint(value: JsonValue) -> int:
        if isinstance(value, bool) or not isinstance(value, int):
            raise TypeError("Expected a JSON integer")
        if not 0 <= value <= 0xFFFFFFFF:
            raise ValueError("JSON integer is outside uint32")
        return value

    @staticmethod
    def text(value: JsonValue) -> str:
        if not isinstance(value, str):
            raise TypeError("Expected a JSON string")
        return value

    @classmethod
    def positions(cls, value: JsonValue) -> tuple[MatrixPosition, ...]:
        result: list[MatrixPosition] = []
        for raw in cls.sequence(value):
            pair = cls.sequence(raw)
            if len(pair) != 2:
                raise ValueError("Expected a matrix coordinate pair")
            row, column = cls.uint(pair[0]), cls.uint(pair[1])
            if row >= 10 or column >= 9:
                raise ValueError("Matrix coordinate is outside GMK70 bounds")
            result.append(MatrixPosition(row, column))
        if len(result) != 72 or len(set(result)) != 72:
            raise ValueError("GMK70 requires 72 unique physical positions")
        return tuple(result)


@dataclass(frozen=True, slots=True)
class ExpectedLayout:
    positions: tuple[MatrixPosition, ...]
    keys: tuple[PhysicalKey, ...]

    @classmethod
    def from_files(cls, keyboard: Path, generated_overlay: Path) -> ExpectedLayout:
        value: JsonValue = json.loads(keyboard.read_text(encoding="utf-8"))
        layouts = JsonSchema.mapping(JsonSchema.mapping(value).get("layouts"))
        layout = JsonSchema.mapping(layouts.get("LAYOUT"))
        raw_positions: list[JsonValue] = [
            JsonSchema.mapping(key).get("matrix")
            for key in JsonSchema.sequence(layout.get("layout"))
        ]
        positions = JsonSchema.positions(raw_positions)
        overlay = generated_overlay.read_text(encoding="utf-8")
        keys: list[PhysicalKey] = []
        for match in re.finditer(
            r"<&key_physical_attrs\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s*>",
            overlay,
        ):
            keys.append(PhysicalKey(*(int(match[index]) for index in range(1, 8))))
        if len(keys) != 72:
            raise ValueError("The exact generated Studio geometry must have 72 keys")
        return cls(positions, tuple(keys))

    def validate(self, physical: PhysicalLayouts) -> None:
        active = physical.layouts[physical.active_index]
        if active.name != "GMK70" or active.keys != self.keys:
            raise ValueError(
                "Runtime physical layout differs from the expected GMK70 build"
            )


@dataclass(frozen=True, slots=True)
class SymbolicBinding:
    behavior: str
    param1: int
    param2: int

    def __post_init__(self) -> None:
        if self.behavior == "gmk_radio" and (
            isinstance(self.param1, bool)
            or isinstance(self.param2, bool)
            or not 0 <= self.param1 <= 6
            or self.param2 != 0
        ):
            raise ValueError("Radio binding requires one device parameter from 0 to 6")


@dataclass(frozen=True, slots=True)
class SymbolicLayer:
    name: str
    bindings: tuple[SymbolicBinding, ...]


@dataclass(frozen=True, slots=True)
class StudioPlan:
    positions: tuple[MatrixPosition, ...]
    layers: tuple[SymbolicLayer, ...]
    source_sha256: str

    @classmethod
    def load(cls, path: Path) -> StudioPlan:
        value: JsonValue = json.loads(path.read_text(encoding="utf-8"))
        document = JsonSchema.mapping(value)
        if (
            document.get("format") != "gmk70-zmk-studio-symbolic-v1"
            or document.get("ready_for_behavior_id_resolution") is not True
        ):
            raise ValueError("Symbolic migration plan is not ready")
        source_hash = JsonSchema.text(document.get("source_sha256"))
        if re.fullmatch(r"[0-9a-f]{64}", source_hash) is None:
            raise ValueError("Migration plan requires a source SHA-256")
        positions = JsonSchema.positions(document.get("positions"))
        layers: list[SymbolicLayer] = []
        for raw_layer in JsonSchema.sequence(document.get("layers")):
            layer = JsonSchema.mapping(raw_layer)
            bindings: list[SymbolicBinding] = []
            for raw_binding in JsonSchema.sequence(layer.get("bindings")):
                binding = JsonSchema.mapping(raw_binding)
                if binding.keys() - {"behavior", "param1", "param2"}:
                    raise ValueError("Unknown symbolic binding field")
                bindings.append(
                    SymbolicBinding(
                        JsonSchema.text(binding.get("behavior")),
                        JsonSchema.uint(binding.get("param1", 0)),
                        JsonSchema.uint(binding.get("param2", 0)),
                    )
                )
            if len(bindings) != 72:
                raise ValueError("Every migration layer requires 72 bindings")
            layers.append(
                SymbolicLayer(JsonSchema.text(layer.get("name")), tuple(bindings))
            )
        if len(layers) != 4:
            raise ValueError("This migration requires exactly four saved Studio layers")
        issues = JsonSchema.sequence(document.get("issues", []))
        for raw_issue in issues:
            issue = JsonSchema.mapping(raw_issue)
            if (
                document.get("unsupported_policy") != "unassigned"
                or issue.get("disposition") != "unassigned"
            ):
                raise ValueError("Migration plan retains an unresolved issue")
            location = JsonSchema.text(issue.get("location"))
            match = re.fullmatch(r"layers\[([0-3])\]\.bindings\[([0-9]+)\]", location)
            if match is None or int(match[2]) >= 72:
                raise ValueError("Unassigned disposition is not a physical binding")
            disposition_binding = layers[int(match[1])].bindings[int(match[2])]
            if disposition_binding != SymbolicBinding("none", 0, 0):
                raise ValueError("Unassigned disposition must contain an empty binding")
        return cls(positions, tuple(layers), source_hash)


@dataclass(frozen=True, slots=True)
class ApplyResult:
    bindings_written: int
    saved_and_reloaded: bool


class PrivateJson:
    @staticmethod
    def write(path: Path, document: dict[str, JsonValue]) -> None:
        serialized = json.dumps(document, indent=2, ensure_ascii=False) + "\n"
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(serialized)


class StudioClient:
    _names: ClassVar[dict[str, str]] = {
        "kp": "Key Press",
        "none": "None",
        "trans": "Transparent",
        "mo": "Momentary Layer",
        "to": "To Layer",
        "tog": "Toggle Layer",
        "rgb_ug": "Underglow",
        "gmk_battery": "GMK70 Battery / Charging",
        "gmk_radio": "GMK70 Bluetooth / Dongle",
    }

    def __init__(self, transport: ByteTransport, timeout: float = 5.0) -> None:
        if not 0 < timeout <= 30:
            raise ValueError("Studio timeout must be between zero and thirty seconds")
        self._transport = transport
        self._timeout = timeout
        self._request_id = 0
        self._framing = StudioFraming()
        self._frames: deque[bytes] = deque()

    def _rpc(self, subsystem: int, operation: int, payload: int | bytes) -> WireMessage:
        self._request_id += 1
        if self._request_id > 0xFFFFFFFF:
            raise ProtocolError("Studio request IDs exhausted")
        body = (
            Protobuf.uint(operation, payload)
            if isinstance(payload, int)
            else Protobuf.blob(operation, payload)
        )
        request = Protobuf.uint(1, self._request_id) + Protobuf.blob(subsystem, body)
        framed = StudioFraming.encode(request)
        deadline = time.monotonic() + self._timeout
        offset = 0
        while offset < len(framed):
            if time.monotonic() >= deadline:
                raise TimeoutError("Studio write deadline expired")
            written = self._transport.write(framed[offset:])
            if not 0 < written <= len(framed) - offset:
                raise ProtocolError("Invalid Studio transport write count")
            offset += written
        skipped = 0
        while time.monotonic() < deadline:
            if not self._frames:
                raw = self._transport.read(4096)
                if raw:
                    self._frames.extend(self._framing.feed(raw))
                continue
            outer = Protobuf.parse(self._frames.popleft())
            if outer.which((1, 2)) == 2:
                skipped += 1
            else:
                response = Protobuf.parse(outer.blob(1))
                if response.uint(1) != self._request_id:
                    skipped += 1
                else:
                    selected_subsystem = response.which((2, 3, 4, 5))
                    if selected_subsystem == 2:
                        error = Protobuf.parse(response.blob(2))
                        raise ProtocolError(
                            f"Studio meta rejection code {error.uint(2)}"
                        )
                    if selected_subsystem != subsystem:
                        raise ProtocolError("Studio response subsystem mismatch")
                    result = Protobuf.parse(response.blob(subsystem))
                    choices = (1, 2) if subsystem == 4 else tuple(range(1, 13))
                    if result.which(choices) != operation:
                        raise ProtocolError("Studio response operation mismatch")
                    return result
            if skipped > 256:
                raise ProtocolError("Too many unrelated Studio responses")
        raise TimeoutError("Studio response deadline expired")

    def read_keymap(self) -> RpcKeymap:
        return RpcKeymap.decode(self._rpc(5, 1, 1).blob(1))

    def get_available_behaviors(self) -> tuple[int, ...]:
        message = Protobuf.parse(self._rpc(4, 1, 1).blob(1))
        identifiers: list[int] = []
        for field in message.fields:
            if field.number != 1:
                continue
            if field.wire_type == WireType.UINT and isinstance(field.value, int):
                identifiers.append(field.value)
            elif field.wire_type == WireType.BLOB and isinstance(field.value, bytes):
                offset = 0
                while offset < len(field.value):
                    identifier, offset = Protobuf.read_varint(field.value, offset)
                    identifiers.append(identifier)
            else:
                raise ProtocolError("Unexpected available-behavior wire type")
        if (
            not identifiers
            or len(identifiers) > 1024
            or len(set(identifiers)) != len(identifiers)
            or any(not 0 <= identifier <= 0xFFFF for identifier in identifiers)
        ):
            raise ProtocolError("Invalid available-behavior IDs")
        return tuple(identifiers)

    def get_behavior_details(self, identifier: int) -> BehaviorDetails:
        details = BehaviorDetails.decode(
            self._rpc(4, 2, Protobuf.uint(1, identifier)).blob(2)
        )
        if details.behavior_id != identifier or not details.display_name:
            raise ProtocolError("Behavior details identity mismatch")
        return details

    def get_physical_layouts(self) -> PhysicalLayouts:
        return PhysicalLayouts.decode(self._rpc(5, 6, 1).blob(6))

    def check_unsaved_changes(self) -> bool:
        value = self._rpc(5, 3, 1).uint(3, required=True)
        if value not in (0, 1):
            raise ProtocolError("Invalid dirty-state boolean")
        return bool(value)

    def set_layer_binding(
        self, layer_id: int, position: int, binding: RpcBinding
    ) -> None:
        if not 0 <= position < 72:
            raise ValueError("GMK70 key position is out of bounds")
        body = (
            Protobuf.uint(1, layer_id)
            + Protobuf.uint(2, position)
            + Protobuf.blob(3, binding.encode())
        )
        if self._rpc(5, 2, body).uint(2, required=True) != 0:
            raise ProtocolError("Studio rejected a layer binding")

    def save_changes(self) -> None:
        response = Protobuf.parse(self._rpc(5, 4, 1).blob(4))
        if response.has(2) or response.uint(1, required=True) != 1:
            raise ProtocolError("Studio did not save changes")

    def discard_changes(self) -> None:
        if self._rpc(5, 5, 1).uint(5, required=True) != 1:
            raise ProtocolError("Studio did not discard/reload changes")

    def reload(self) -> RpcKeymap:
        self.discard_changes()
        return self.read_keymap()

    def snapshot(self, output: Path) -> RpcKeymap:
        dirty = self.check_unsaved_changes()
        keymap = self.read_keymap()
        PrivateJson.write(
            output,
            {
                "format": "gmk70-zmk-studio-backup-v1",
                "unsaved_changes": dirty,
                "keymap": keymap.document(),
            },
        )
        return keymap

    def _resolve(self, plan: StudioPlan, before: RpcKeymap) -> RpcKeymap:
        if len(before.layers) != 4 or any(
            len(layer.bindings) != 72 for layer in before.layers
        ):
            raise ValueError(
                "Runtime Studio map must have exactly four layers of 72 keys"
            )
        if len(plan.layers) != 4 or any(
            len(layer.bindings) != 72 for layer in plan.layers
        ):
            raise ValueError("Migration plan must have exactly 288 bindings")
        catalog = tuple(
            self.get_behavior_details(identifier)
            for identifier in self.get_available_behaviors()
        )
        by_id = {details.behavior_id: details for details in catalog}
        symbols = {
            binding.behavior for layer in plan.layers for binding in layer.bindings
        }
        resolved: dict[str, BehaviorDetails] = {}
        for symbol in symbols:
            if symbol not in self._names:
                raise ValueError("Unknown symbolic behavior")
            matches = tuple(
                details
                for details in catalog
                if details.display_name == self._names[symbol]
            )
            if len(matches) != 1:
                raise ValueError(
                    "A required behavior display name is missing or ambiguous"
                )
            resolved[symbol] = matches[0]
        layer_ids = frozenset(layer.layer_id for layer in before.layers)
        for layer in before.layers:
            for binding in layer.bindings:
                details = by_id.get(binding.behavior_id)
                if details is None or not details.allows(binding, layer_ids):
                    raise ValueError("The fresh original map cannot be safely restored")
        layers: list[RpcLayer] = []
        for old_layer, source in zip(before.layers, plan.layers, strict=True):
            bindings: list[RpcBinding] = []
            for symbolic in source.bindings:
                if not (
                    0 <= symbolic.param1 <= 0xFFFFFFFF
                    and 0 <= symbolic.param2 <= 0xFFFFFFFF
                ):
                    raise ValueError("Symbolic parameters must fit uint32")
                parameter = symbolic.param1
                if symbolic.behavior in ("mo", "to", "tog"):
                    if not 0 <= parameter < len(before.layers):
                        raise ValueError("Symbolic layer reference is out of bounds")
                    parameter = before.layers[parameter].layer_id
                binding = RpcBinding(
                    resolved[symbolic.behavior].behavior_id, parameter, symbolic.param2
                )
                if not resolved[symbolic.behavior].allows(binding, layer_ids):
                    raise ValueError(
                        "A resolved binding does not match runtime behavior metadata"
                    )
                bindings.append(binding)
            layers.append(RpcLayer(old_layer.layer_id, old_layer.name, tuple(bindings)))
        return RpcKeymap(
            tuple(layers), before.available_layers, before.max_layer_name_length
        )

    def _write_bindings(self, keymap: RpcKeymap) -> None:
        for layer in keymap.layers:
            for position, binding in enumerate(layer.bindings):
                self.set_layer_binding(layer.layer_id, position, binding)

    def _verify_saved(self, expected: RpcKeymap, geometry: ExpectedLayout) -> None:
        if self.reload() != expected or self.check_unsaved_changes():
            raise ProtocolError("Saved Studio keymap verification failed")
        geometry.validate(self.get_physical_layouts())

    def apply(
        self,
        plan: StudioPlan,
        geometry: ExpectedLayout,
        backup: Path,
        only_behavior: str | None = None,
    ) -> ApplyResult:
        if only_behavior is not None and (
            only_behavior not in self._names
            or not any(
                binding.behavior == only_behavior
                for layer in plan.layers
                for binding in layer.bindings
            )
        ):
            raise ValueError(
                "Selective apply requires a registered symbolic behavior present in the plan"
            )
        if backup.exists():
            raise FileExistsError("Fresh backup output already exists")
        if (
            plan.positions != geometry.positions
            or len(set(plan.positions)) != 72
            or len(geometry.keys) != 72
        ):
            raise ValueError(
                "Migration and maintained physical positions do not match one-to-one"
            )
        if self.check_unsaved_changes():
            raise ValueError("Studio has unsaved changes; apply is forbidden")
        before = self.read_keymap()
        geometry.validate(self.get_physical_layouts())
        target = self._resolve(plan, before)
        if only_behavior is not None:
            target = RpcKeymap(
                tuple(
                    RpcLayer(
                        old.layer_id,
                        old.name,
                        tuple(
                            resolved if symbolic.behavior == only_behavior else current
                            for symbolic, resolved, current in zip(
                                source.bindings,
                                proposed.bindings,
                                old.bindings,
                                strict=True,
                            )
                        ),
                    )
                    for source, proposed, old in zip(
                        plan.layers, target.layers, before.layers, strict=True
                    )
                ),
                before.available_layers,
                before.max_layer_name_length,
            )
        if self.check_unsaved_changes() or self.read_keymap() != before:
            raise ValueError("Studio changed during migration preflight")
        PrivateJson.write(
            backup,
            {
                "format": "gmk70-zmk-studio-backup-v1",
                "unsaved_changes": False,
                "source_sha256": plan.source_sha256,
                "keymap": before.document(),
            },
        )
        try:
            self._write_bindings(target)
            if self.read_keymap() != target:
                raise ProtocolError("Studio staging verification failed")
            self.save_changes()
            self._verify_saved(target, geometry)
        except (OSError, TimeoutError, ProtocolError, ValueError) as failure:
            try:
                self._write_bindings(before)
                self.save_changes()
                self._verify_saved(before, geometry)
            except (OSError, TimeoutError, ProtocolError, ValueError) as restoration:
                raise StudioApplyError(False) from restoration
            raise StudioApplyError(True) from failure
        return ApplyResult(288, True)

    @classmethod
    def main(cls) -> None:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--port", required=True)
        subcommands = parser.add_subparsers(dest="action", required=True)
        snapshot = subcommands.add_parser("snapshot")
        snapshot.add_argument("--output", required=True, type=Path)
        apply = subcommands.add_parser("apply")
        for name in ("plan", "keyboard", "geometry", "backup", "result"):
            apply.add_argument(f"--{name}", required=True, type=Path)
        apply.add_argument("--only-behavior", choices=tuple(cls._names))
        arguments = parser.parse_args(namespace=CliArguments())
        plan: StudioPlan | None = None
        geometry: ExpectedLayout | None = None
        if arguments.action == "apply":
            plan = StudioPlan.load(arguments.plan)
            geometry = ExpectedLayout.from_files(arguments.keyboard, arguments.geometry)
            if arguments.backup.exists() or arguments.result.exists():
                parser.exit(2, "Private backup/result output already exists.\n")
        elif arguments.output.exists():
            parser.exit(2, "Private snapshot output already exists.\n")
        transport = SerialByteTransport(arguments.port)
        try:
            client = cls(transport)
            if plan is not None and geometry is not None:
                result = client.apply(
                    plan,
                    geometry,
                    arguments.backup,
                    only_behavior=arguments.only_behavior,
                )
                PrivateJson.write(
                    arguments.result,
                    {
                        "format": "gmk70-zmk-studio-apply-v1",
                        "bindings_written": result.bindings_written,
                        "saved_and_reloaded": result.saved_and_reloaded,
                    },
                )
                print(
                    f"Saved and reloaded {result.bindings_written} Studio bindings; layer names and order preserved."
                )
            else:
                keymap = client.snapshot(arguments.output)
                print(f"Private Studio snapshot saved: {len(keymap.layers)} layers.")
        except StudioApplyError as error:
            parser.exit(
                2,
                f"Studio apply failed. Original restored: {error.rollback_succeeded}.\n",
            )
        except (OSError, TimeoutError, ProtocolError, ValueError, TypeError) as error:
            parser.exit(2, f"Studio operation failed: {type(error).__name__}.\n")
        finally:
            transport.close()


@dataclass
class CliArguments:
    action: str = ""
    port: str = ""
    output: Path = Path()
    plan: Path = Path()
    keyboard: Path = Path()
    geometry: Path = Path()
    backup: Path = Path()
    result: Path = Path()
    only_behavior: str | None = None


if __name__ == "__main__":
    StudioClient.main()

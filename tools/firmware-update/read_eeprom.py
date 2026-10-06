"""Read logical EEPROM from either half through left USB, without writes or resets."""

import argparse
import hashlib
import json
import secrets
import struct
import sys
import time
import zlib
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from enum import IntEnum
from pathlib import Path
from typing import ClassVar

from update import Link, Op, Updater, UsbLink


class QueryOp(IntEnum):
    INFO = 8
    READ = 9
    LOG_INFO = 10
    LOG_READ = 11


@dataclass(frozen=True)
class Metadata:
    size: int
    crc32: int
    protocol: int
    layout: int


@dataclass(frozen=True)
class Snapshot:
    target: int
    metadata: Metadata
    data: bytes

    def document(self) -> dict[str, object]:
        result: dict[str, object] = {
            "target": "right" if self.target else "left",
            "metadata": asdict(self.metadata),
            "sha256": hashlib.sha256(self.data).hexdigest(),
            "logical_eeprom_hex": self.data.hex(),
        }
        if self.metadata.layout == 15 and len(self.data) == 4096:
            result["decoded"] = decode_v15(self.data)
        return result


def decode_v15(data: bytes) -> dict[str, object]:
    """Decode stored values, retaining all bytes and original keycode numbering."""
    if len(data) != 4096:
        raise ValueError("V15 logical EEPROM must contain 4096 bytes")
    keycodes = struct.unpack_from(">360H", data, 38)
    encoders = struct.unpack_from(">8H", data, 758)
    flags = int.from_bytes(data[19:23], "little")
    sleep_selector = (data[18] >> 1) & 7
    sleep_timeouts_ms = (0, 60000, 180000, 300000, 600000, 1200000, 1800000, 300000)
    return {
        "core_marker": f"0x{int.from_bytes(data[:2], 'little'):04X}",
        "keymap_config": f"0x{int.from_bytes(data[4:6], 'little'):04X}",
        "via_magic_hex": data[34:37].hex(),
        "layout_options": data[37],
        "board": {
            "flags": flags,
            "gui_locked": bool(flags & 4),
            "flipped": bool(flags & 16),
        },
        "radio": {
            "selected_device": data[15],
            "previous_wireless_device": data[16],
            "last_bt_slot": data[17],
            "flags_and_sleep_selector": data[18],
            "initialized_flag": bool(data[18] & 1),
            "sleep_selector": sleep_selector,
            "idle_sleep_timeout_ms": sleep_timeouts_ms[sleep_selector],
        },
        "rgblight": {
            "enabled": bool(data[8] & 1),
            "mode": data[8] >> 1,
            "hue": data[9],
            "saturation": data[10],
            "value": data[11],
        },
        "rgb_matrix": {
            "enable": data[28] & 3,
            "mode": data[28] >> 2,
            "hue": data[29],
            "saturation": data[30],
            "value": data[31],
            "speed": data[32],
            "flags": data[33],
        },
        "matrix_keycodes": [
            [
                [
                    f"0x{code:04X}"
                    for code in keycodes[
                        layer * 90 + row * 9 : layer * 90 + (row + 1) * 9
                    ]
                ]
                for row in range(10)
            ]
            for layer in range(4)
        ],
        "encoders": [
            [[f"0x{code:04X}" for code in encoders[layer * 2 : (layer + 1) * 2]]]
            for layer in range(4)
        ],
        "macros": {"max_count": 16, "capacity": 3322, "buffer_hex": data[774:].hex()},
    }


class EepromReader:
    info_op: ClassVar[QueryOp] = QueryOp.INFO
    read_op: ClassVar[QueryOp] = QueryOp.READ
    query_name: ClassVar[str] = "EEPROM"
    required_firmware: ClassVar[str] = "EEPROM-query updater"
    minimum_size: ClassVar[int] = 1
    maximum_size: ClassVar[int] = 65536

    def __init__(self, link: Link) -> None:
        self._link = link
        self._token = secrets.randbelow(0xFFFFFFFF) + 1

    @classmethod
    def packet(
        cls,
        target: int,
        operation: QueryOp,
        token: int,
        offset: int = 0,
        count: int = 0,
    ) -> bytes:
        if (
            target not in (0, 1)
            or operation not in (cls.info_op, cls.read_op)
            or not 1 <= token <= 0xFFFFFFFF
            or not 0 <= offset <= 0xFFFFFFFF
            or (operation == cls.info_op and (offset != 0 or count != 0))
            or (operation == cls.read_op and not 1 <= count <= 16)
        ):
            raise ValueError(f"Invalid {cls.query_name} query")
        return (
            bytes((0x7F, 0x70, 1, target, operation, count, 0, 0))
            + struct.pack("<II", token, offset)
            + bytes(16)
        )

    def command(
        self, target: int, operation: QueryOp, offset: int = 0, count: int = 0
    ) -> bytes:
        token = self._token
        self._token = token % 0xFFFFFFFF + 1
        original = self.packet(target, operation, token, offset, count)
        packet = original
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            try:
                reply = self._link.exchange(packet)
            except OSError:
                packet = original
                time.sleep(0.02)
                continue
            if len(reply) != 32 or reply[:4] != original[:4]:
                raise RuntimeError(
                    f"No matching {self.query_name} response; install {self.required_firmware} firmware on both halves"
                )
            if reply[5] == 1:
                packet = Updater.packet(target, Op.STATUS)
                time.sleep(0.01)
                continue
            if reply[5] == 7:
                raise RuntimeError(
                    f"{self.query_name} target unavailable; check wired mode, the split cable, and {self.required_firmware} support on both halves"
                )
            if reply[4] != operation or struct.unpack_from("<II", reply, 8) != (
                token,
                offset,
            ):
                # STATUS can expose a completed request from another process.
                # Never accept stale bytes from an earlier read of this offset.
                packet = original
                time.sleep(0.01)
                continue
            if reply[5] == 2:
                raise RuntimeError(
                    f"{self.query_name} queries are unsupported; install {self.required_firmware} firmware on both halves"
                )
            if reply[5] == 3:
                raise RuntimeError(
                    f"A firmware update is active; wait for it to finish before reading {self.query_name}"
                )
            if reply[5] != 0:
                raise RuntimeError(
                    f"Target {target}: {self.query_name} {operation.name} failed, code {reply[5]}"
                )
            if reply[7] != (0 if target else 3):
                raise RuntimeError("Wrong half or master role in EEPROM response")
            if reply[6] != count or (
                operation == self.read_op and any(reply[16 + count :])
            ):
                raise RuntimeError(
                    f"Invalid {self.query_name} response length or padding"
                )
            return reply
        raise TimeoutError(
            f"{self.query_name} query timed out; check the wired split connection and close other HID tools"
        )

    def inspect(self, target: int) -> Metadata:
        reply = self.command(target, self.info_op)
        metadata = Metadata(*struct.unpack_from("<IIII", reply, 16))
        if (
            metadata.protocol != 1
            or not self.minimum_size <= metadata.size <= self.maximum_size
        ):
            raise RuntimeError(
                f"Unsupported {self.query_name} protocol or logical size"
            )
        return metadata

    def snapshot(self, target: int, attempts: int = 3) -> Snapshot:
        if attempts < 1:
            raise ValueError("At least one snapshot attempt is required")
        for _ in range(attempts):
            before = self.inspect(target)
            data = bytearray()
            for offset in range(0, before.size, 16):
                count = min(16, before.size - offset)
                reply = self.command(target, self.read_op, offset, count)
                data.extend(reply[16 : 16 + count])
            after = self.inspect(target)
            if before == after and zlib.crc32(data) == before.crc32:
                return Snapshot(target, before, bytes(data))
        raise RuntimeError(
            f"{self.query_name} changed during reading or CRC verification failed; stop changing settings and retry"
        )


def backup_document(snapshots: list[Snapshot]) -> dict[str, object]:
    return {
        "format": "gmk70-logical-eeprom-backup",
        "version": 1,
        "captured_at": datetime.now(UTC).isoformat(),
        "halves": {
            "right" if item.target else "left": item.document() for item in snapshots
        },
    }


def write_backup(path: Path, snapshots: list[Snapshot]) -> None:
    # Never overwrite a previous backup, including a symlink to one.
    document = json.dumps(backup_document(snapshots), indent=2) + "\n"
    with path.open("x", encoding="utf-8") as output:
        output.write(document)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("right", "left", "both"), default="right")
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="New JSON backup file; existing files are never overwritten",
    )
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        parser.error("Backup output already exists")
    link = UsbLink()
    try:
        reader = EepromReader(link)
        targets = (1, 0) if args.target == "both" else (int(args.target == "right"),)
        snapshots = [reader.snapshot(target) for target in targets]
        write_backup(args.output, snapshots)
        for snapshot in snapshots:
            print(
                f"{'Right' if snapshot.target else 'Left'}: {len(snapshot.data)} bytes, CRC32 {snapshot.metadata.crc32:08X}, verified"
            )
        if len(snapshots) == 2:
            print(
                f"Half EEPROM contents identical: {snapshots[0].data == snapshots[1].data}"
            )
        print(f"Saved {args.output}")
    finally:
        link.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        sys.exit(str(error))

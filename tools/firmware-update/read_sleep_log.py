"""Read persistent sleep records after a power cycle; no device writes or resets."""

import argparse
import hashlib
import json
import struct
import sys
import zlib
from datetime import UTC, datetime
from pathlib import Path
from typing import ClassVar

from read_eeprom import EepromReader, Metadata, QueryOp, Snapshot
from update import UsbLink

REASONS = {
    0: "idle timeout",
    1: "radio disconnected",
    2: "legacy radio idle",
    3: "critical battery",
    4: "host suspend",
    5: "USB special sleep",
}
TRIGGERS = {1: "before STOP", 2: "after wake", 3: "peer communication failed"}
STATES = ("NORMAL", "PRESLEEP", "STOP", "WAKEUP")


class SleepLogReader(EepromReader):
    info_op: ClassVar[QueryOp] = QueryOp.LOG_INFO
    read_op: ClassVar[QueryOp] = QueryOp.LOG_READ
    query_name: ClassVar[str] = "Persistent sleep log"
    required_firmware: ClassVar[str] = "persistent-log debug-updater"
    minimum_size: ClassVar[int] = 0
    maximum_size: ClassVar[int] = 8192

    def inspect(self, target: int) -> Metadata:
        info = super().inspect(target)
        if info.size not in (0, 8192):
            raise RuntimeError("Unexpected persistent journal size")
        return info


def decode_journal(data: bytes) -> dict[str, object]:
    if len(data) not in (0, 8192):
        raise ValueError("Persistent journal must be empty or 8192 bytes")
    records: list[dict[str, object]] = []
    invalid = erased = 0
    for offset in range(0, len(data), 256):
        page = data[offset : offset + 256]
        if page == b"\xff" * 256:
            erased += 1
            continue
        generation = int.from_bytes(page[4:8], "little")
        if (
            page[:4] != b"GSL1"
            or not generation
            or page[38] > 16
            or page[39] != 12
            or page[252:] != bytes(4)
            or int.from_bytes(page[248:252], "little") != zlib.crc32(page[:248])
        ):
            invalid += 1
            continue
        session, uptime, sequence, timeout, idle = struct.unpack_from("<IIIII", page, 8)
        attempts, failures = struct.unpack_from("<II", page, 44)
        events = []
        for i in range(page[38]):
            seq, time, kind, a, b, c = struct.unpack_from("<IIBBBB", page, 56 + i * 12)
            events.append(
                {
                    "sequence": seq,
                    "uptime_ms": time,
                    "event": kind,
                    "a": a,
                    "b": b,
                    "c": c,
                }
            )
        records.append(
            {
                "slot": offset // 256,
                "generation": generation,
                "recording_session": session,
                "uptime_ms": uptime,
                "last_event_sequence": sequence,
                "configured_timeout_ms": timeout,
                "accepted_idle_ms": idle,
                "battery_percent": page[28],
                "sleep_selector": page[29],
                "state": STATES[page[30] & 3],
                "status_bits": page[30],
                "low_battery_sleep": bool(page[30] & 64),
                "device": page[31],
                "radio": page[32],
                "wake_cause": page[33],
                "pins": page[34],
                "half": "left" if page[35] else "right",
                "sleep_reason": (
                    "critical battery (peer latch)"
                    if not page[36] and page[30] & 64
                    else "manual request (unspecified)"
                    if not page[36] and page[53]
                    else REASONS.get(page[36], f"unknown({page[36]})")
                ),
                "trigger": TRIGGERS.get(page[37], f"unknown({page[37]})"),
                "peer_seen": bool(page[43]),
                "peer_status_bits": page[41],
                "peer_loop_age_ms": min(page[42] * 16, 4080),
                "rpc_attempts": attempts,
                "rpc_failures": failures,
                "reason_context": page[52],
                "accepted_manual": bool(page[53]),
                "split_connected": bool(page[54]),
                "events": events,
            }
        )
    # Generations use the same modular ordering as the firmware journal.
    if records:
        newest = int(str(records[0]["generation"]))
        for record in records[1:]:
            candidate = int(str(record["generation"]))
            if 0 < (candidate - newest) & 0xFFFFFFFF < 0x80000000:
                newest = candidate
        records.sort(
            key=lambda record: (newest - int(str(record["generation"]))) & 0xFFFFFFFF,
            reverse=True,
        )
    return {
        "records": records,
        "erased_pages": erased,
        "uncommitted_or_corrupt_pages": invalid,
    }


def document(snapshots: list[Snapshot], errors: dict[str, str]) -> dict[str, object]:
    halves = {}
    for snapshot in snapshots:
        status = snapshot.metadata.layout
        halves["right" if snapshot.target else "left"] = {
            "journal_size": len(snapshot.data),
            "journal_crc32": snapshot.metadata.crc32,
            "journal_sha256": hashlib.sha256(snapshot.data).hexdigest(),
            "journal_hex": snapshot.data.hex(),
            "flash_jedec_id": f"{status >> 8:06X}",
            "storage_ready": bool(status & 1),
            "region_owned": bool(status & 2),
            "write_io_error": bool(status & 4),
            **decode_journal(snapshot.data),
        }
    return {
        "format": "gmk70-persistent-sleep-log",
        "version": 1,
        "captured_at": datetime.now(UTC).isoformat(),
        "halves": halves,
        "errors": errors,
    }


def write_report(path: Path, report: dict[str, object]) -> None:
    with path.open("x", encoding="utf-8") as output:
        output.write(json.dumps(report, indent=2) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("both", "left", "right"), default="both")
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="New JSON file; existing reports are never overwritten",
    )
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        parser.error("Report output already exists")
    snapshots: list[Snapshot] = []
    errors: dict[str, str] = {}
    link = UsbLink()
    try:
        reader = SleepLogReader(link)
        targets = (0, 1) if args.target == "both" else (int(args.target == "right"),)
        for target in targets:
            name = "right" if target else "left"
            try:
                snapshot = reader.snapshot(target)
                snapshots.append(snapshot)
                decoded = decode_journal(snapshot.data)
                print(
                    f"{name}: {len(snapshot.data)} bytes, CRC verified; flash={snapshot.metadata.layout >> 8:06X} flags={snapshot.metadata.layout & 255:02X}"
                )
                records = decoded["records"]
                assert isinstance(records, list)
                for record in records:
                    print(
                        f"  #{record['generation']} session={record['recording_session']} {record['trigger']}: reason={record['sleep_reason']} configured={record['configured_timeout_ms']}ms idle={record['accepted_idle_ms']}ms manual={record['accepted_manual']} battery={record['battery_percent']}% low_battery={record['low_battery_sleep']}"
                    )
            except (OSError, RuntimeError, ValueError) as error:
                errors[name] = str(error)
                print(f"{name}: {error}", file=sys.stderr)
    finally:
        link.close()
    write_report(args.output, document(snapshots, errors))
    print(f"Saved {args.output}; no device settings or logs were changed")
    if errors:
        sys.exit(1)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)

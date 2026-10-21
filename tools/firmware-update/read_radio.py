"""Read RAM radio diagnostics over USB; no settings writes or radio probes."""

import argparse
import json
import secrets
import struct
import time
from pathlib import Path
from typing import Protocol, TypedDict, cast

PREFIX = b"\x22RD\x01"
COUNTERS = (
    "uart_rx_bytes",
    "unknown_leading_bytes",
    "ack_packets",
    "valid_frames",
    "checksum_failures",
    "raw_headers",
    "raw_oversize",
    "raw_31",
    "raw_32",
    "raw_other_length",
    "raw_dispatched",
    "raw_tx_requests",
)


class Snapshot(TypedDict):
    format: str
    version: int
    uptime_ms: int
    radio_firmware_version_received: bool
    radio_firmware_version: int | None
    last_bluetooth_device: int
    maximum_raw_payload_length: int
    total: dict[str, int]
    bluetooth: dict[str, int]


class Comparison(TypedDict):
    reset_or_wrap_suspected: bool
    total: dict[str, int] | None
    bluetooth: dict[str, int] | None


class Link(Protocol):
    def exchange(self, packet: bytes) -> bytes: ...


class RadioReader:
    def __init__(self, link: Link) -> None:
        self.link = link

    def snapshot(self) -> Snapshot:
        token = secrets.randbelow(65536)
        words: list[int] = []
        for page in range(5):
            request = PREFIX + bytes((page, 0)) + struct.pack("<H", token) + bytes(24)
            response = self.link.exchange(request)
            if (
                len(response) != 32
                or response[:5] != request[:5]
                or response[6:8] != request[6:8]
            ):
                raise RuntimeError(
                    "No matching radio diagnostic response; check the diagnostic firmware and USB connection"
                )
            if response[5]:
                reasons = {
                    1: "Select wired mode and connect the left master by USB",
                    2: "Unsupported diagnostic page",
                    3: "Unsupported diagnostic protocol",
                    4: "Diagnostic snapshot changed; close Vial and other readers",
                }
                raise RuntimeError(
                    reasons.get(response[5], "Unknown radio diagnostic error")
                )
            words.extend(struct.unpack_from("<6I", response, 8))
        if (
            words[1] not in (0, 1)
            or words[2] > 255
            or words[3] > 5
            or words[4] > 255
            or words[5] != 2
        ):
            raise RuntimeError("Invalid radio diagnostic metadata")
        return Snapshot(
            format="gmk70-radio-diagnostics",
            version=1,
            uptime_ms=words[0],
            radio_firmware_version_received=bool(words[1]),
            radio_firmware_version=words[2] if words[1] else None,
            last_bluetooth_device=words[3],
            maximum_raw_payload_length=words[4],
            total=dict(zip(COUNTERS, words[6:18], strict=True)),
            bluetooth=dict(zip(COUNTERS, words[18:30], strict=True)),
        )


def compare(before: Snapshot, after: Snapshot) -> Comparison:
    reset = after["uptime_ms"] < before["uptime_ms"] or any(
        current[name] < previous[name]
        for previous, current in (
            (before["total"], after["total"]),
            (before["bluetooth"], after["bluetooth"]),
        )
        for name in COUNTERS
    )
    return Comparison(
        reset_or_wrap_suspected=reset,
        total=None
        if reset
        else {name: after["total"][name] - before["total"][name] for name in COUNTERS},
        bluetooth=None
        if reset
        else {
            name: after["bluetooth"][name] - before["bluetooth"][name]
            for name in COUNTERS
        },
    )


def load_snapshot(path: Path) -> Snapshot:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if (
        not isinstance(value, dict)
        or value.get("format") != "gmk70-radio-diagnostics"
        or value.get("version") != 1
    ):
        raise ValueError("Not a version-1 radio diagnostic snapshot")
    for group in ("total", "bluetooth"):
        counters = value.get(group)
        if (
            not isinstance(counters, dict)
            or set(counters) != set(COUNTERS)
            or any(
                type(item) is not int or not 0 <= item <= 0xFFFFFFFF
                for item in counters.values()
            )
        ):
            raise ValueError("Invalid diagnostic counters in baseline")
    if (
        type(value.get("uptime_ms")) is not int
        or not 0 <= value["uptime_ms"] <= 0xFFFFFFFF
    ):
        raise ValueError("Invalid diagnostic uptime in baseline")
    return cast(Snapshot, value)


class UsbRadioLink:
    def __init__(self) -> None:
        import hid

        candidates = [
            d
            for d in hid.enumerate(0x342D, 0xE491)
            if d["usage_page"] == 0xFF60
            and d["usage"] == 0x61
            and d["manufacturer_string"] in ("HS", "LEO")
        ]
        if len(candidates) != 1:
            raise RuntimeError(
                "Connect exactly one GMK70 left half by USB, in wired mode"
            )
        self.device = hid.device()
        self.device.open_path(candidates[0]["path"])

    def exchange(self, packet: bytes) -> bytes:
        if (
            len(packet) != 32
            or packet[:4] != PREFIX
            or packet[4] >= 5
            or packet[5]
            or packet[8:] != bytes(24)
        ):
            raise ValueError("Only payload-free radio diagnostic getters are allowed")
        if self.device.write(b"\0" + packet) != 33:
            raise OSError("Incomplete USB diagnostic write")
        deadline = time.monotonic() + 2
        while (remaining := deadline - time.monotonic()) > 0:
            response = bytes(self.device.read(32, max(1, int(remaining * 1000))))
            if not response:
                break
            if (
                len(response) == 32
                and response[0] == 0xFF
                and response[1:5] == packet[1:5]
            ):
                raise RuntimeError(
                    "Install the optional radio diagnostic firmware first"
                )
            if (
                len(response) == 32
                and response[:5] == packet[:5]
                and response[6:8] == packet[6:8]
            ):
                return response
        raise RuntimeError(
            "Radio diagnostic USB read timed out; close Vial and check the diagnostic build"
        )

    def close(self) -> None:
        self.device.close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args(argv)
    link: UsbRadioLink | None = None
    try:
        if args.output.exists():
            raise ValueError("Output already exists; choose a new filename")
        before = load_snapshot(args.baseline) if args.baseline else None
        link = UsbRadioLink()
        snapshot = RadioReader(link).snapshot()
        document: dict[str, object] = dict(snapshot)
        if before is not None:
            document["comparison"] = compare(before, snapshot)
        text = json.dumps(document, indent=2) + "\n"
        with args.output.open("x", encoding="utf-8") as handle:
            handle.write(text)
        print(text, end="")
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"{error}\n")
    finally:
        if link is not None:
            link.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

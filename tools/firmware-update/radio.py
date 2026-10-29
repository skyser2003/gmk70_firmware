"""Explicit USB radio control and read-only status; never reset, flash or retry actions."""

import argparse
import json
import math
import secrets
import struct
import sys
import time
from dataclasses import dataclass
from enum import IntEnum
from typing import Literal, TypedDict

from read_health import Exchange
from update import UsbLink


class RadioAction(IntEnum):
    STATUS = 0
    SELECT = 1
    PAIR = 2
    MARKER = 3


class RadioResult(IntEnum):
    OK = 0
    BUSY = 1
    UNAVAILABLE = 2


@dataclass(frozen=True, slots=True)
class RadioRequest:
    action: RadioAction
    channel: int
    nonce: int

    def __post_init__(self) -> None:
        if not isinstance(self.action, RadioAction):
            raise TypeError("Unknown radio action")
        if isinstance(self.nonce, bool) or not 0 < self.nonce < 2**32:
            raise ValueError("Radio request needs a nonzero uint32 nonce")
        if (
            isinstance(self.channel, bool)
            or not 0 <= self.channel <= 6
            or (self.action == RadioAction.PAIR and self.channel == 0)
            or (
                self.action in (RadioAction.STATUS, RadioAction.MARKER) and self.channel
            )
        ):
            raise ValueError("Invalid channel for radio action")

    def encode(self) -> bytes:
        return (
            bytes((0x7F, 0x72, 1, 0, 0x22, self.action, self.channel, 0))
            + struct.pack("<I", self.nonce)
            + bytes(20)
        )

    @classmethod
    def decode(cls, packet: bytes) -> "RadioRequest":
        if (
            len(packet) != 32
            or packet[:5] != b"\x7f\x72\x01\x00\x22"
            or packet[7] != 0
            or any(packet[12:])
        ):
            raise ValueError("Malformed radio request")
        return cls(
            RadioAction(packet[5]), packet[6], int.from_bytes(packet[8:12], "little")
        )


class RadioFields(TypedDict):
    nonce: int
    result: int
    waiting: bool
    available: bool
    qualification: bool
    selected: int
    active: int
    connection: int
    queued: int
    errors: int
    selections: int
    reports: int


class RadioEvidence(TypedDict):
    action: str
    read_only: bool
    target: Literal["left"]
    radio: RadioFields


@dataclass(frozen=True, slots=True)
class RadioSnapshot:
    nonce: int
    result: RadioResult
    waiting: bool
    available: bool
    qualification: bool
    selected: int
    active: int
    connection: int
    queued: int
    errors: int
    selections: int
    reports: int

    @classmethod
    def decode(cls, packet: bytes, nonce: int) -> "RadioSnapshot":
        RadioRequest(RadioAction.STATUS, 0, nonce)
        if (
            len(packet) != 32
            or packet[:5] != b"\x7f\x72\x01\x00\x22"
            or packet[5] > 2
            or packet[6] & ~7
            or packet[7] != 3
            or int.from_bytes(packet[8:12], "little") != nonce
            or packet[12:16] != b"RADI"
            or packet[16] > 6
            or packet[17] > 6
            or packet[18] > 4
            or packet[19] > 8
        ):
            raise RuntimeError("Malformed or mismatched LEFT radio reply")
        return cls(
            nonce,
            RadioResult(packet[5]),
            bool(packet[6] & 1),
            bool(packet[6] & 2),
            bool(packet[6] & 4),
            packet[16],
            packet[17],
            packet[18],
            packet[19],
            int.from_bytes(packet[20:24], "little"),
            int.from_bytes(packet[24:28], "little"),
            int.from_bytes(packet[28:32], "little"),
        )

    def evidence(self, action: RadioAction) -> RadioEvidence:
        return {
            "action": action.name.lower(),
            "read_only": action == RadioAction.STATUS,
            "target": "left",
            "radio": {
                "nonce": self.nonce,
                "result": self.result,
                "waiting": self.waiting,
                "available": self.available,
                "qualification": self.qualification,
                "selected": self.selected,
                "active": self.active,
                "connection": self.connection,
                "queued": self.queued,
                "errors": self.errors,
                "selections": self.selections,
                "reports": self.reports,
            },
        }


class RadioUsbLink(UsbLink):
    """Reuse HID discovery/framing with nonce correlation for radio error replies."""

    def exchange(self, packet: bytes) -> bytes:
        # Updater errors correlate session+offset at bytes8..15. Radio uses a
        # nonce and RADI magic there, so use its nonce without resending actions.
        request = RadioRequest.decode(packet)
        self._send_packet(packet)
        deadline = time.monotonic() + 2
        while (remaining := deadline - time.monotonic()) > 0:
            reply = self._read_packet(max(1, int(remaining * 1000)))
            if not reply or time.monotonic() >= deadline:
                break
            if (
                len(reply) == 32
                and reply[:5] == packet[:5]
                and int.from_bytes(reply[8:12], "little") == request.nonce
            ):
                return reply
        raise OSError("Radio USB response timed out; the action was not retried")


class RadioReader:
    """One exchange per explicit action; a timeout never triggers a repeat."""

    def __init__(self, link: Exchange) -> None:
        self._link = link

    def _request(self, action: RadioAction, channel: int, nonce: int) -> RadioSnapshot:
        packet = RadioRequest(action, channel, nonce).encode()
        snapshot = RadioSnapshot.decode(self._link.exchange(packet), nonce)
        if snapshot.result != RadioResult.OK:
            raise RuntimeError(
                f"Radio action {snapshot.result.name.lower()}; not retried"
            )
        return snapshot

    def status(self, nonce: int) -> RadioSnapshot:
        return self._request(RadioAction.STATUS, 0, nonce)

    def select(self, channel: int, nonce: int) -> RadioSnapshot:
        return self._request(RadioAction.SELECT, channel, nonce)

    def pair(self, channel: int, nonce: int) -> RadioSnapshot:
        return self._request(RadioAction.PAIR, channel, nonce)

    def marker(self, nonce: int) -> RadioSnapshot:
        return self._request(RadioAction.MARKER, 0, nonce)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action",
        choices=("status", "select", "pair", "marker"),
        nargs="?",
        default="status",
    )
    parser.add_argument("--channel", type=int, choices=range(7))
    parser.add_argument(
        "--samples",
        type=int,
        default=1,
        help="Read-only status samples, 1..120; actions run once",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=0.2,
        help="Seconds between status samples, 0.05..60",
    )
    arguments = parser.parse_args()
    action = RadioAction[arguments.action.upper()]
    channel: int | None = arguments.channel
    samples: int = arguments.samples
    interval: float = arguments.interval
    if (
        not 1 <= samples <= 120
        or not math.isfinite(interval)
        or not 0.05 <= interval <= 60
        or (action != RadioAction.STATUS and samples != 1)
        or (action in (RadioAction.SELECT, RadioAction.PAIR) and channel is None)
        or (action in (RadioAction.STATUS, RadioAction.MARKER) and channel is not None)
        or (action == RadioAction.PAIR and channel == 0)
    ):
        parser.error(
            "Invalid radio action, channel or bounded status polling arguments"
        )
    link = RadioUsbLink()
    reader = RadioReader(link)
    try:
        for index in range(samples):
            nonce = secrets.randbelow(0xFFFFFFFF) + 1
            if action == RadioAction.STATUS:
                snapshot = reader.status(nonce)
            elif action == RadioAction.SELECT:
                assert channel is not None
                snapshot = reader.select(channel, nonce)
            elif action == RadioAction.PAIR:
                assert channel is not None
                snapshot = reader.pair(channel, nonce)
            else:
                snapshot = reader.marker(nonce)
            print(json.dumps(snapshot.evidence(action), indent=2))
            if index + 1 < samples:
                time.sleep(interval)
    finally:
        link.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, TypeError, RuntimeError) as error:
        sys.exit(str(error))

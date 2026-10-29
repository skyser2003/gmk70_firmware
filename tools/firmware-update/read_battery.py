"""Read-only LEFT battery snapshot through ZMK namespace 0x72; no retries or resets."""

import argparse
import json
import secrets
import struct
import sys
from dataclasses import dataclass
from typing import Literal, TypedDict

from read_health import Exchange
from update import UsbLink


class BatteryFields(TypedDict):
    nonce: int
    percentage: int | None
    valid: bool
    cable: bool
    full: bool
    age_ms: int
    queries: int
    replies: int


class BatteryEvidence(TypedDict):
    read_only: Literal[True]
    target: Literal["left"]
    battery: BatteryFields


@dataclass(frozen=True)
class BatterySnapshot:
    nonce: int
    percentage: int | None
    valid: bool
    cable: bool
    full: bool
    age_ms: int
    queries: int
    replies: int

    @classmethod
    def decode(cls, packet: bytes, nonce: int) -> "BatterySnapshot":
        if not 0 < nonce < 2**32:
            raise ValueError("Battery snapshot needs a nonzero uint32 nonce")
        if (
            len(packet) != 32
            or packet[:8] != b"\x7f\x72\x01\x00\x21\x00\x00\x03"
            or int.from_bytes(packet[8:12], "little") != nonce
            or packet[12:16] != b"BATT"
            or packet[18:20] != b"\0\0"
        ):
            raise RuntimeError("Malformed or mismatched LEFT battery reply")
        percentage, flags = packet[16], packet[17]
        age_ms = int.from_bytes(packet[20:24], "little")
        valid, cable, full = bool(flags & 1), bool(flags & 2), bool(flags & 4)
        if (
            flags & ~7
            or (full and not cable)
            or (valid and (percentage > 100 or age_ms > 10000))
            or (not valid and percentage != 255)
        ):
            raise RuntimeError("Inconsistent battery percentage, freshness or flags")
        return cls(
            nonce,
            percentage if valid else None,
            valid,
            cable,
            full,
            age_ms,
            int.from_bytes(packet[24:28], "little"),
            int.from_bytes(packet[28:32], "little"),
        )

    def evidence(self) -> BatteryEvidence:
        return {
            "read_only": True,
            "target": "left",
            "battery": {
                "nonce": self.nonce,
                "percentage": self.percentage,
                "valid": self.valid,
                "cable": self.cable,
                "full": self.full,
                "age_ms": self.age_ms,
                "queries": self.queries,
                "replies": self.replies,
            },
        }


class BatteryReader:
    """Uses an existing link and issues one local read-only telemetry request."""

    def __init__(self, link: Exchange) -> None:
        self._link = link

    def sample(self, nonce: int) -> BatterySnapshot:
        if not 0 < nonce < 2**32:
            raise ValueError("Battery snapshot needs a nonzero uint32 nonce")
        packet = b"\x7f\x72\x01\x00\x21\0\0\0" + struct.pack("<I", nonce) + bytes(20)
        return BatterySnapshot.decode(self._link.exchange(packet), nonce)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    link = UsbLink()
    try:
        snapshot = BatteryReader(link).sample(secrets.randbelow(0xFFFFFFFF) + 1)
        print(json.dumps(snapshot.evidence(), indent=2))
    finally:
        link.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        sys.exit(str(error))

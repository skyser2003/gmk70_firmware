"""Read-only ZMK namespace 0x72 health evidence; never reconnect or reset."""

import argparse
import json
import math
import secrets
import struct
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Protocol

from update import ZMK_BOOT_CRC, Op, Reply, Updater, UsbLink


class Exchange(Protocol):
    def exchange(self, packet: bytes) -> bytes: ...


def header(packet: bytes, target: int, nonce: int) -> None:
    if (
        len(packet) != 32
        or packet[:5] != bytes((0x7F, 0x72, 1, target, 0x20))
        or packet[6] != 0
        or struct.unpack_from("<I", packet, 8)[0] != nonce
    ):
        raise RuntimeError("Malformed or mismatched namespace 0x72 health reply")


@dataclass(frozen=True)
class Health:
    nonce: int
    uptime_ms: int
    feed_calls: int
    matrix_events: int | None
    flags: int
    role: int

    @classmethod
    def decode(
        cls, packet: bytes, target: int, nonce: int, *, staging: bool = False
    ) -> "Health":
        header(packet, target, nonce)
        magic, uptime, feeds, flags, matrix = struct.unpack_from("<5I", packet, 12)
        disabled = bool(flags & 16)
        if (
            packet[5] != 0
            or packet[7] != (0 if target else 3)
            or magic != 0x4B4D5A47
            or flags & ~31
            or not flags & 2
            or bool(flags & 8) != staging
            or bool(flags & 1) == (staging or disabled)
            or (disabled and (target != 0 or staging))
            or ((staging or disabled) and feeds != 0)
        ):
            raise RuntimeError(
                "Wrong ZMK health magic, role or permanent/staging flags"
            )
        return cls(nonce, uptime, feeds, matrix or None, flags, packet[7])


class HealthReader:
    """Accepts an existing link so a qualification observer can share one HID handle."""

    def __init__(self, link: Exchange) -> None:
        self.link = link
        self.transport_retries = 0
        self.relay_retries = 0

    def sample(
        self, target: int, nonce: int, *, staging: bool = False, timeout: float = 4.0
    ) -> Health:
        """Bound acceptance/polling; a generic link retains its own exchange timeout."""
        if (
            target not in (0, 1)
            or not 0 < nonce < 2**32
            or not math.isfinite(timeout)
            or timeout <= 0
        ):
            raise ValueError(
                "Health needs a target, nonzero uint32 nonce and finite deadline"
            )
        packet = (
            bytes((0x7F, 0x72, 1, target, 0x20, 0, 0, 0))
            + struct.pack("<I", nonce)
            + bytes(20)
        )
        deadline = time.monotonic() + timeout
        relay_attempts = 0
        while time.monotonic() < deadline:
            try:
                response = self.link.exchange(packet)
            except OSError:
                self.transport_retries += 1
                time.sleep(0.02)
                continue
            if time.monotonic() > deadline:
                raise TimeoutError("Health reply arrived after the sample deadline")
            header(response, target, nonce)
            if response[5] in (1, 7):
                # Generic BUSY/UNAVAILABLE from the USB master has no final peer
                # health magic or role. Correlation is still mandatory.
                if response[7] not in (0 if target else 3, 3):
                    raise RuntimeError("Malformed health pending role")
                if response[5] == 7:
                    self.relay_retries += 1
                    relay_attempts += 1
                    if relay_attempts > 3:
                        raise RuntimeError(
                            "Peer health unavailable after bounded relay retries"
                        )
                    # Non-BUSY failures are cached by the receiver. Request a
                    # fresh peer query rather than polling that cached failure.
                    nonce = nonce % 0xFFFFFFFF + 1
                    packet = packet[:8] + struct.pack("<I", nonce) + packet[12:]
                time.sleep(0.01)
                continue
            return Health.decode(response, target, nonce, staging=staging)
        raise TimeoutError("Health request timed out; no reconnect or reset attempted")

    def identity(self, target: int, *, timeout: float = 4.0) -> Reply:
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("Identity requires a finite positive timeout")
        packet = Updater.packet(target, Op.INFO)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                reply = Reply.decode(self.link.exchange(packet), target)
            except OSError:
                self.transport_retries += 1
                packet = Updater.packet(target, Op.INFO)
                continue
            if time.monotonic() > deadline:
                raise TimeoutError("Identity reply arrived after its deadline")
            if reply.result == 1:
                packet = Updater.packet(target, Op.STATUS)
                time.sleep(0.01)
                continue
            if reply.result == 7:
                self.relay_retries += 1
                packet = Updater.packet(target, Op.INFO)
                time.sleep(0.01)
                continue
            if (
                reply.result != 0
                or reply.operation != Op.INFO
                or reply.state != 0
                or reply.role != (0 if target else 3)
                or reply.abi != 1
                or reply.boot_crc != ZMK_BOOT_CRC
                or not 1024 <= reply.size <= 0x1E000
                or reply.size % 4
            ):
                raise RuntimeError(
                    "Wrong half, resident, updater ABI or active update during health observation"
                )
            return reply
        raise TimeoutError("Identity timed out; no reconnect attempted")

    def observe(
        self,
        targets: tuple[int, ...] = (0, 1),
        *,
        duration: float = 60.0,
        interval: float = 1.0,
        max_gap: float = 2.0,
    ) -> dict[str, object]:
        if (
            not math.isfinite(duration)
            or duration < 60
            or not 0 < interval < max_gap <= 2
            or not targets
            or len(set(targets)) != len(targets)
            or any(t not in (0, 1) for t in targets)
        ):
            raise ValueError(
                "Permanent evidence requires >=60 seconds and sample gaps <=2 seconds"
            )
        identities = {target: self.identity(target) for target in targets}
        records: list[dict[str, object]] = []
        previous: dict[int, tuple[float, Health]] = {}
        first: dict[int, float] = {}
        started = time.monotonic()
        nonce = secrets.randbelow(0xFFFFFFFF) + 1
        while True:
            for target in targets:
                nonce = nonce % 0xFFFFFFFF + 1
                health = self.sample(target, nonce)
                nonce = health.nonce
                now = time.monotonic()
                if health.uptime_ms > 2000:
                    first.setdefault(target, now)
                if target in previous:
                    when, old = previous[target]
                    validate_progress(old, health, now - when, max_gap)
                previous[target] = (now, health)
                records.append(
                    {
                        "target": target,
                        "elapsed_seconds": now - started,
                        **asdict(health),
                    }
                )
            if all(
                t in first and previous[t][0] - first[t] >= duration for t in targets
            ):
                break
            if len(first) != len(targets) and time.monotonic() - started > 10:
                raise RuntimeError(
                    "Health uptime never exceeded the 2-second startup guard"
                )
            time.sleep(interval)
        after = {target: self.identity(target) for target in targets}
        if after != identities:
            raise RuntimeError("Image/resident identity changed during observation")
        return {
            "read_only": True,
            "duration_seconds": time.monotonic() - started,
            "max_gap_seconds": max_gap,
            "verified_window_seconds": min(previous[t][0] - first[t] for t in targets),
            "startup_uptime_guard_ms": 2000,
            "identities": {str(t): asdict(r) for t, r in identities.items()},
            "transport_retries": self.transport_retries,
            "relay_retries": self.relay_retries,
            "reconnects": 0,
            "matrix_events_zero_means": "unavailable; no physical key evidence",
            "samples": records,
        }


def validate_progress(
    previous: Health, current: Health, elapsed: float, max_gap: float
) -> None:
    uptime = (current.uptime_ms - previous.uptime_ms) & 0xFFFFFFFF
    feeds = (current.feed_calls - previous.feed_calls) & 0xFFFFFFFF
    if not 0 < elapsed <= max_gap:
        raise RuntimeError("Health sample gap exceeds the evidence bound")
    if not 0 < uptime < 2**31 or abs(uptime - elapsed * 1000) > max(250, elapsed * 200):
        raise RuntimeError("Uptime stopped or reboot/discontinuity detected")
    if bool(previous.flags & 16) != bool(current.flags & 16):
        raise RuntimeError("Watchdog policy changed during observation")
    if current.flags & 16:
        if previous.feed_calls != 0 or current.feed_calls != 0:
            raise RuntimeError("Watchdog rollback requires zero feed calls")
    elif not 0 < feeds < 2**31:
        raise RuntimeError("Watchdog feed calls stopped or reset")
    if current.nonce == previous.nonce:
        raise RuntimeError("Continuity requires a fresh health nonce")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("both", "left", "right"), default="both")
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument(
        "--inspect-only",
        action="store_true",
        help="One identity and health snapshot; no continuity claim",
    )
    parser.add_argument(
        "--qualification",
        action="store_true",
        help="Expect staging mode (requires --inspect-only)",
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.qualification and not args.inspect_only:
        parser.error(
            "Qualification cannot prove permanent watchdog feeding; use --inspect-only"
        )
    if not args.inspect_only and args.duration < 60:
        parser.error("Continuous evidence requires at least 60 seconds")
    targets = (0, 1) if args.target == "both" else (int(args.target == "right"),)
    link = UsbLink()
    try:
        reader = HealthReader(link)
        evidence: dict[str, object]
        if args.inspect_only:
            evidence = {
                "read_only": True,
                "continuous": False,
                "targets": {
                    str(t): {
                        "identity": asdict(reader.identity(t)),
                        "health": asdict(
                            reader.sample(
                                t,
                                secrets.randbelow(0xFFFFFFFF) + 1,
                                staging=args.qualification,
                            )
                        ),
                    }
                    for t in targets
                },
            }
        else:
            evidence = reader.observe(targets, duration=args.duration)
        result = json.dumps(evidence, indent=2) + "\n"
        if args.output:
            args.output.write_text(result)
        print(result, end="")
    finally:
        link.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        sys.exit(str(error))

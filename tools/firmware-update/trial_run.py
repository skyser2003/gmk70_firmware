"""Explicit GMK70 trial execution. Never invoked by build, test or setup commands."""

import argparse
import json
import secrets
import struct
import sys
import time
from collections.abc import Callable
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from enum import IntEnum, IntFlag
from pathlib import Path
from typing import TypedDict

from trial import TrialPackage, upload_packets
from update import Link, UsbLink


class TrialOp(IntEnum):
    STATUS = 1
    BEGIN = 2
    DATA = 3
    SEAL = 4
    ABORT = 6
    INFO = 7
    RUN = 0x10
    PROBE = 0x11
    RESULT = 0x12
    CAPS = 0x13
    FALLBACK_TEST = 0x14
    TRACE = 0x15
    FAULT_TEST = 0x16


class TrialCapability(IntFlag):
    PROBE = 1
    RUN = 2
    EARLY = 4
    FALLBACK = 8
    FALLBACK_TEST = 16
    FAULT_TEST = 32


def nonce() -> int:
    return secrets.randbelow(0xFFFFFFFF) + 1


def correlated(response: bytes, original: bytes, current: bytes) -> bool:
    if len(response) != 32 or response[:4] != original[:4]:
        return False
    if response[5] not in (0, 1):
        completed = response[4] == original[4] and response[8:16] == original[8:16]
        polling = (
            current[4] == TrialOp.STATUS
            and response[4] == TrialOp.STATUS
            and response[8:16] == current[8:16]
        )
        return completed or polling
    if response[4] != original[4]:
        return False
    if original[4] in (TrialOp.CAPS, TrialOp.RESULT, TrialOp.TRACE):
        if response[8:16] != original[8:16]:
            return False
    elif (
        original[4] not in (TrialOp.INFO, TrialOp.ABORT)
        and response[8:12] != original[8:12]
    ):
        return False
    if response[5] == 0 and original[4] == TrialOp.DATA:
        expected = struct.unpack_from("<I", original, 12)[0] + original[5]
        return bool(struct.unpack_from("<I", response, 12)[0] == expected)
    return True


class TrialUsbLink(UsbLink):
    """Reuse USB discovery/lifecycle, with trial nonce and BUSY correlation."""

    def exchange(self, packet: bytes) -> bytes:
        if len(packet) != 32 or packet[:3] != b"\x7f\x71\x02":
            raise ValueError("Expected a 32-byte trial ABI 2 packet")
        original = packet
        if packet[4] == TrialOp.STATUS:
            if (
                self._pending_request is not None
                and self._pending_request[:4] == packet[:4]
            ):
                original = self._pending_request
        else:
            self._pending_request = packet
        if self._device.write(b"\0" + packet) != 33:
            raise OSError("Incomplete USB write")
        deadline = time.monotonic() + 2
        while (remaining := deadline - time.monotonic()) > 0:
            response = bytes(self._device.read(32, max(1, int(remaining * 1000))))
            if not response:
                break
            if correlated(response, original, packet):
                return response
        raise OSError("Trial USB response timed out")


@dataclass(frozen=True)
class TrialReply:
    operation: int
    result: int
    state: int
    role: int
    session: int
    received: int
    payload: bytes

    @classmethod
    def decode(cls, packet: bytes) -> "TrialReply":
        session, received = struct.unpack_from("<II", packet, 8)
        return cls(
            packet[4],
            packet[5],
            packet[6],
            packet[7],
            session,
            received,
            packet[16:32],
        )


@dataclass(frozen=True)
class Identity:
    size: int
    crc: int
    abi: int
    boot_crc: int
    role: int


@dataclass(frozen=True)
class TrialCapabilities:
    bits: int
    fallback_ms: int
    ram_start: int
    ram_end: int


@dataclass(frozen=True)
class TrialTrace:
    address: int
    value: int
    cfsr: int
    pc: int
    flags: int


@dataclass(frozen=True)
class TrialResult:
    action: int
    phase: int
    reset_flags: int
    size: int
    crc: int
    role: int

    def verify(
        self, action: int, expect_reset: str, image: TrialPackage | None
    ) -> None:
        flags = 4 if expect_reset == "watchdog" else 8
        if self.action != action or self.phase != 3:
            raise RuntimeError(
                "Trial did not produce the expected terminal RETURNED result"
            )
        if self.reset_flags not in (flags, flags | 32):
            raise RuntimeError(f"Unexpected trial reset cause: {self.reset_flags:#x}")
        size, crc = (len(image.image), image.manifest["crc32"]) if image else (0, 0)
        if (self.size, self.crc) != (size, crc):
            raise RuntimeError("Trial result does not identify the requested image")


class TrialReport(TypedDict):
    success: bool
    action: str
    target: str
    expected_reset: str
    started_at: str
    finished_at: str
    elapsed_seconds: float
    capabilities: dict[str, int]
    before: dict[str, int]
    after: dict[str, int]
    result: dict[str, int]
    trace: dict[str, int]
    package_sha256: str | None
    retries: int


class TrialClient:
    def __init__(
        self,
        link: Link,
        *,
        now: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self._link, self._now, self._sleep = link, now, sleep
        self.retries = 0
        self.last_report: dict[str, object] = {}

    @staticmethod
    def packet(
        target: int,
        operation: TrialOp,
        session: int = 0,
        offset: int = 0,
        data: bytes = b"",
    ) -> bytes:
        if (
            target not in (0, 1)
            or len(data) > 16
            or not 0 <= session < 2**32
            or not 0 <= offset < 2**32
        ):
            raise ValueError("Invalid trial target, session, offset or payload")
        return (
            bytes((0x7F, 0x71, 2, target, operation, len(data), 0, 0))
            + struct.pack("<II", session, offset)
            + data.ljust(16, b"\0")
        )

    def command(
        self,
        target: int,
        operation: TrialOp,
        session: int = 0,
        offset: int = 0,
        data: bytes = b"",
        *,
        timeout: float = 8,
    ) -> TrialReply:
        original = self.packet(target, operation, session, offset, data)
        current = original
        deadline = self._now() + timeout
        while self._now() < deadline:
            try:
                raw = self._link.exchange(current)
            except OSError:
                self.retries += 1
                current = original
                self._sleep(0.02)
                continue
            if not correlated(raw, original, current):
                self.retries += 1
                current = original
                self._sleep(0.005)
                continue
            reply = TrialReply.decode(raw)
            if reply.result == 1:
                current = self.packet(target, TrialOp.STATUS)
                self._sleep(0.002)
                continue
            if reply.result == 7:
                self.retries += 1
                current = original
                self._sleep(0.05)
                continue
            if reply.result == 8:
                raise RuntimeError(
                    "Vial is locked; physically unlock it in Vial, close Vial and retry"
                )
            if reply.result:
                raise RuntimeError(
                    f"Target {target}: {operation.name} failed, code {reply.result}, state {reply.state}"
                )
            if reply.role != (3 if target == 0 else 0):
                raise RuntimeError(
                    "Trial response came from the wrong half or transport role"
                )
            return reply
        raise TimeoutError(
            f"Target {target}: {operation.name} timed out; trial ABI 2 backend required"
        )

    def inspect(self, target: int) -> tuple[Identity, TrialReply]:
        reply = self.command(target, TrialOp.INFO)
        size, crc, abi, boot_crc = struct.unpack("<IIII", reply.payload)
        if abi != 2 or not 1024 <= size <= 0x1E000:
            raise RuntimeError(
                "A trial ABI 2 backend with a valid running application is required"
            )
        return Identity(size, crc, abi, boot_crc, reply.role), reply

    def result(self, target: int) -> TrialResult:
        reply = self.command(target, TrialOp.RESULT, nonce())
        packed, flags, size, crc = struct.unpack("<IIII", reply.payload)
        if packed >> 16:
            raise RuntimeError("Invalid trial result encoding")
        return TrialResult(packed & 0xFF, packed >> 8, flags, size, crc, reply.role)

    def capabilities(self, target: int) -> TrialCapabilities:
        try:
            reply = self.command(target, TrialOp.CAPS, nonce(), nonce())
        except (RuntimeError, TimeoutError) as error:
            message = (
                f"Read-only CAPS support is required before trial operations: {error}"
            )
            if isinstance(error, TimeoutError):
                raise TimeoutError(message) from error
            raise RuntimeError(message) from error
        return TrialCapabilities(*struct.unpack("<IIII", reply.payload))

    def trace(self, target: int) -> TrialTrace:
        first = self.command(target, TrialOp.TRACE, nonce(), 0)
        second = self.command(target, TrialOp.TRACE, nonce(), 1)
        address, value, cfsr, pc = struct.unpack("<IIII", first.payload)
        flags, reserved1, reserved2, reserved3 = struct.unpack("<IIII", second.payload)
        if reserved1 or reserved2 or reserved3 or flags & ~0x1FF or flags & 0xFF > 5:
            raise RuntimeError("Invalid retained fault trace")
        return TrialTrace(address, value, cfsr, pc, flags)

    def fault(self, target: int) -> TrialReport:
        return self._execute(target, None, "software", TrialOp.FAULT_TEST)

    def probe(self, target: int) -> TrialReport:
        return self._execute(target, None, "watchdog", TrialOp.PROBE)

    def fallback(self, target: int) -> TrialReport:
        return self._execute(target, None, "software", TrialOp.FALLBACK_TEST)

    def run(self, package: TrialPackage, *, expect_reset: str) -> TrialReport:
        # Revalidate an object supplied by another caller before opening a session.
        if package != TrialPackage.create(package.image, package.manifest["target"]):
            raise ValueError("Invalid trial package")
        return self._execute(
            int(package.manifest["target"] == "right"),
            package,
            expect_reset,
            TrialOp.RUN,
        )

    def _execute(
        self,
        target: int,
        package: TrialPackage | None,
        expect_reset: str,
        operation: TrialOp,
    ) -> TrialReport:
        if expect_reset not in ("software", "watchdog"):
            raise ValueError("Choose software or watchdog reset expectation")
        started = self._now()
        action = {
            TrialOp.PROBE: "probe",
            TrialOp.RUN: "run",
            TrialOp.FALLBACK_TEST: "fallback",
            TrialOp.FAULT_TEST: "fault",
        }[operation]
        self.last_report = {
            "success": False,
            "action": action,
            "target": "right" if target else "left",
            "expected_reset": expect_reset,
            "started_at": datetime.now(UTC).isoformat(),
            "package_sha256": package.manifest["sha256"] if package else None,
        }
        try:
            capabilities = self.capabilities(target)
            self.last_report["capabilities"] = asdict(capabilities)
            required = {
                TrialOp.PROBE: TrialCapability.PROBE,
                TrialOp.RUN: TrialCapability.RUN,
                TrialOp.FALLBACK_TEST: TrialCapability.FALLBACK_TEST,
                TrialOp.FAULT_TEST: TrialCapability.FAULT_TEST,
            }[operation]
            if not capabilities.bits & required:
                raise RuntimeError(
                    f"Target {target}: {operation.name} capability is unavailable"
                )
            before, status = self.inspect(target)
            self.last_report["before"] = asdict(before)
            if status.state != 0:
                raise RuntimeError(
                    "An earlier update session is active; wait 11 seconds without commands"
                )
            session = nonce()
            if package:
                for packet in upload_packets(package, session):
                    upload_operation = TrialOp(packet[4])
                    offset = struct.unpack_from("<I", packet, 12)[0]
                    reply = self.command(
                        target,
                        upload_operation,
                        session,
                        offset,
                        packet[16 : 16 + packet[5]],
                    )
                    if upload_operation == TrialOp.SEAL and reply.state != 2:
                        raise RuntimeError(
                            "Trial receiver did not acknowledge SEALED state"
                        )
            self.command(target, operation, session)
            self.last_report["acknowledged"] = True
            # The peer has a bounded polling pause; left USB must enumerate again.
            self._sleep(9)
            deadline = self._now() + 45
            reconnect = target == 0
            while self._now() < deadline:
                try:
                    if reconnect:
                        try:
                            self._link.reconnect()
                        except (OSError, RuntimeError):
                            self.retries += 1
                            self._sleep(0.25)
                            continue
                        reconnect = False
                    after, status = self.inspect(target)
                    self.last_report["after"] = asdict(after)
                    if after != before:
                        raise RuntimeError(
                            "Application or resident identity changed across the trial"
                        )
                    result = self.result(target)
                    self.last_report["result"] = asdict(result)
                    trace = (
                        self.trace(target)
                        if capabilities.bits & TrialCapability.FAULT_TEST
                        else None
                    )
                    self.last_report["trace"] = asdict(trace) if trace else {}
                    if status.state != 0 or result.phase in (1, 2):
                        self._sleep(0.25)
                        continue
                    result.verify(1 if package else 2, expect_reset, package)
                    if operation == TrialOp.FAULT_TEST and (
                        trace is None
                        or trace.flags & 0xFF not in (1, 5)
                        or not trace.cfsr & 0x10000
                        or not 0x08002000 <= trace.pc < 0x08002000 + before.size
                    ):
                        raise RuntimeError(
                            "FAULT_TEST did not return a captured undefined instruction"
                        )
                    report: TrialReport = {
                        "success": True,
                        "action": action,
                        "target": "right" if target else "left",
                        "expected_reset": expect_reset,
                        "started_at": str(self.last_report["started_at"]),
                        "finished_at": datetime.now(UTC).isoformat(),
                        "elapsed_seconds": self._now() - started,
                        "capabilities": asdict(capabilities),
                        "before": asdict(before),
                        "after": asdict(after),
                        "result": asdict(result),
                        "trace": asdict(trace) if trace else {},
                        "package_sha256": package.manifest["sha256"]
                        if package
                        else None,
                        "retries": self.retries,
                    }
                    self.last_report = dict(report)
                    return report
                except (OSError, TimeoutError):
                    self.retries += 1
                    reconnect = True
                    self._sleep(0.25)
            raise TimeoutError(
                "Trial recovery did not return a verified result before the deadline"
            )
        except (OSError, RuntimeError, TimeoutError, ValueError) as error:
            self.last_report.update(
                error=str(error),
                retries=self.retries,
                elapsed_seconds=self._now() - started,
                finished_at=datetime.now(UTC).isoformat(),
            )
            raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    probe = commands.add_parser(
        "probe", help="Explicitly start a no-feed watchdog reset probe"
    )
    probe.add_argument("--target", choices=("left", "right"), required=True)
    fallback = commands.add_parser(
        "fallback", help="Explicitly prove the bounded software reset fallback"
    )
    fallback.add_argument("--target", choices=("left", "right"), required=True)
    fault = commands.add_parser(
        "fault", help="Explicitly qualify active-probe fault capture and reset"
    )
    fault.add_argument("--target", choices=("left", "right"), required=True)
    run = commands.add_parser(
        "run", help="Explicitly upload, seal and execute one staging package"
    )
    run.add_argument("package", type=Path)
    run.add_argument("--expect-reset", choices=("watchdog", "software"), required=True)
    result = commands.add_parser(
        "result", help="Read the retained trial result without a reset"
    )
    result.add_argument("--target", choices=("left", "right"), required=True)
    for command in (probe, fallback, fault, run, result):
        command.add_argument("--report", type=Path)
    args = parser.parse_args()
    package = TrialPackage.load(args.package) if args.command == "run" else None
    link = TrialUsbLink()
    client = TrialClient(link)
    report: object
    exit_code = 0
    try:
        if args.command == "run":
            assert package is not None
            report = client.run(package, expect_reset=args.expect_reset)
        elif args.command == "probe":
            report = client.probe(int(args.target == "right"))
        elif args.command == "fault":
            report = client.fault(int(args.target == "right"))
        elif args.command == "fallback":
            report = client.fallback(int(args.target == "right"))
        else:
            target = int(args.target == "right")
            identity, _ = client.inspect(target)
            report = {
                "target": args.target,
                "identity": asdict(identity),
                "result": asdict(client.result(target)),
                "retries": client.retries,
            }
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        report = client.last_report or {"success": False, "error": str(error)}
        exit_code = 1
    finally:
        link.close()
    output = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.write_text(output)
    print(output, end="")
    return exit_code


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        sys.exit(str(error))

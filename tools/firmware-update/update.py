"""Explicit device updater. Never called by build, test, setup, or migration tools."""

import argparse
import hashlib
import json
import secrets
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Protocol, TypedDict, cast


class Manifest(TypedDict):
    format: int
    board: str
    mcu: str
    profile: str
    app_address: int
    app_size: int
    app_crc: int
    app_sha256: str
    boot_crc: int
    boot_sha256: str
    install_sha256: str


@dataclass(frozen=True)
class Package:
    image: bytes
    manifest: Manifest

    @classmethod
    def load(cls, directory: Path) -> "Package":
        manifest = cast(Manifest, json.loads((directory / "update.json").read_text()))
        image = (directory / "application.bin").read_bytes()
        resident = (directory / "resident.bin").read_bytes()
        if (
            manifest["format"] != 1
            or manifest["board"] != "zuoya/gmk70"
            or manifest["mcu"] != "WB32FQ95-256K"
            or manifest["profile"] not in ("debug", "release", "vial")
            or manifest["app_address"] != 0x08002000
            or not 1024 <= len(image) <= 0x1E000
            or len(image) % 4
            or manifest["app_size"] != len(image)
            or manifest["app_crc"] != zlib.crc32(image)
            or manifest["app_sha256"] != hashlib.sha256(image).hexdigest()
            or not 64 <= len(resident) <= 0x2000
        ):
            raise ValueError("Invalid or corrupt GMK70 updater package")
        boot = resident.ljust(0x2000, b"\xff")
        stack, reset = struct.unpack_from("<II", image)
        if (
            manifest["boot_crc"] != zlib.crc32(boot)
            or manifest["boot_sha256"] != hashlib.sha256(boot).hexdigest()
            or not 0x20000000 < stack <= 0x20007000
            or stack % 8
            or not reset & 1
            or not 0x08002000 <= reset < 0x08002000 + len(image)
        ):
            raise ValueError("Wrong installer or application link address")
        return cls(image, manifest)


class Op(IntEnum):
    STATUS = 1
    BEGIN = 2
    DATA = 3
    SEAL = 4
    INSTALL = 5
    ABORT = 6
    INFO = 7


@dataclass(frozen=True)
class Reply:
    operation: int
    result: int
    state: int
    role: int
    session: int
    received: int
    size: int
    crc: int
    abi: int
    boot_crc: int

    @classmethod
    def decode(cls, packet: bytes, target: int) -> "Reply":
        if len(packet) != 32 or packet[:4] != bytes((0x7F, 0x70, 1, target)):
            raise RuntimeError(
                "No matching updater response; install updater firmware on both halves first"
            )
        fields = struct.unpack_from("<IIIIII", packet, 8)
        return cls(packet[4], packet[5], packet[6], packet[7], *fields)


class Link(Protocol):
    def exchange(self, packet: bytes) -> bytes: ...
    def reconnect(self) -> None: ...
    def close(self) -> None: ...


class UsbLink:
    def __init__(self) -> None:
        # Lazy import: package validation and tests do not enumerate any devices.
        import hid

        self._hid = hid
        self._device = hid.device()
        self.reconnect()

    def reconnect(self) -> None:
        self._device.close()
        candidates = [
            item
            for item in self._hid.enumerate(0x342D, 0xE491)
            if item["usage_page"] == 0xFF60
            and item["usage"] == 0x61
            and item["manufacturer_string"] in ("HS", "LEO")
        ]
        if len(candidates) != 1:
            raise RuntimeError(
                "Connect exactly one GMK70 left half by USB, in wired mode; keep the halves linked"
            )
        self._device = self._hid.device()
        self._device.open_path(candidates[0]["path"])

    def exchange(self, packet: bytes) -> bytes:
        if len(packet) != 32:
            raise ValueError("Updater packets must be 32 bytes")
        # Ignore stale replies left by an interrupted host process.
        for _ in range(32):
            if not self._device.read(32, 1):
                break
        if self._device.write(b"\0" + packet) != 33:
            raise OSError("Incomplete USB write")
        response = bytes(self._device.read(32, 2000))
        if len(response) != 32:
            raise OSError("USB response timed out")
        return response

    def close(self) -> None:
        self._device.close()


class Updater:
    def __init__(self, link: Link) -> None:
        self._link = link

    @staticmethod
    def packet(
        target: int, operation: Op, session: int = 0, offset: int = 0, data: bytes = b""
    ) -> bytes:
        if target not in (0, 1) or len(data) > 16:
            raise ValueError("Invalid updater target or data length")
        return (
            bytes((0x7F, 0x70, 1, target, operation, len(data), 0, 0))
            + struct.pack("<II", session, offset)
            + data.ljust(16, b"\0")
        )

    def command(
        self,
        target: int,
        operation: Op,
        session: int = 0,
        offset: int = 0,
        data: bytes = b"",
    ) -> Reply:
        original = self.packet(target, operation, session, offset, data)
        packet = original
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            try:
                reply = Reply.decode(self._link.exchange(packet), target)
            except OSError:
                # Commands are idempotent for the same session/offset. Retrying
                # the original command recovers a dropped request or response.
                packet = original
                time.sleep(0.02)
                continue
            if reply.result == 1:
                packet = self.packet(target, Op.STATUS)
                time.sleep(0.01)
                continue
            if reply.result == 7:
                packet = original
                time.sleep(0.05)
                continue
            if reply.operation != operation:
                packet = original
                continue
            if reply.result != 0:
                if reply.result == 8:
                    raise RuntimeError(
                        "Vial is locked: unlock in the Vial GUI by holding Escape + left Shift, "
                        "then close Vial and retry the updater"
                    )
                raise RuntimeError(
                    f"Target {target}: operation {operation.name} failed, code {reply.result}, state {reply.state}"
                )
            if operation not in (Op.INFO, Op.ABORT) and reply.session != session:
                raise RuntimeError("Mismatched update session")
            return reply
        raise TimeoutError(
            f"Target {target}: {operation.name} timed out; no automatic fallback or protection changes"
        )

    def inspect(self, target: int, package: Package) -> Reply:
        reply = self.command(target, Op.INFO)
        expected_role = 3 if target == 0 else 0
        if (
            reply.role != expected_role
            or reply.abi != 1
            or reply.boot_crc != package.manifest["boot_crc"]
        ):
            raise RuntimeError(
                "Wrong half, updater version, or resident installer; direct USB installation is required"
            )
        return reply

    def install(self, target: int, package: Package) -> None:
        before = self.inspect(target, package)
        if before.state != 0:
            raise RuntimeError(
                "An earlier session is active; wait 11 seconds without sending commands and retry"
            )
        session = secrets.randbelow(0xFFFFFFFF) + 1
        begin = struct.pack("<III", len(package.image), package.manifest["app_crc"], 1)
        self.command(target, Op.BEGIN, session, data=begin)
        committed = False
        try:
            for offset in range(0, len(package.image), 16):
                data = package.image[offset : offset + 16]
                reply = self.command(target, Op.DATA, session, offset, data)
                if reply.received != offset + len(data):
                    raise RuntimeError(
                        "Device did not acknowledge the expected image offset"
                    )
                if offset % 4096 == 0:
                    print(
                        f"{'Right' if target else 'Left'}: {offset}/{len(package.image)} bytes",
                        flush=True,
                    )
            self.command(target, Op.SEAL, session)
            # From this point a lost response may mean the install is committed.
            # Do not send ABORT after requesting INSTALL.
            committed = True
            try:
                self.command(target, Op.INSTALL, session)
            except (OSError, TimeoutError):
                pass
        finally:
            if not committed:
                try:
                    self.command(target, Op.ABORT, session)
                except (OSError, RuntimeError, TimeoutError):
                    pass
        self.verify_restart(target, package)

    def verify_restart(self, target: int, package: Package) -> None:
        time.sleep(1)
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            try:
                if target == 0:
                    self._link.reconnect()
                reply = self.inspect(target, package)
                if (
                    reply.state == 0
                    and reply.size == len(package.image)
                    and reply.crc == package.manifest["app_crc"]
                ):
                    print(
                        f"{'Right' if target else 'Left'}: running image CRC verified",
                        flush=True,
                    )
                    return
            except (OSError, RuntimeError, TimeoutError):
                pass
            time.sleep(0.25)
        raise TimeoutError(
            "Could not verify the running image. Stop here; do not update the other half"
        )

    def install_both(self, package: Package) -> None:
        if package.manifest["profile"] not in ("debug", "vial"):
            raise ValueError(
                "The left half must retain USB update support; use a debug or Vial updater package for --target both"
            )
        # Check both before the first write; install/verify right before left.
        self.inspect(0, package)
        self.inspect(1, package)
        self.install(1, package)
        self.install(0, package)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--target", choices=("both", "right", "left"), default="both")
    parser.add_argument(
        "--inspect",
        action="store_true",
        help="Only read updater INFO; no flash or reset",
    )
    parser.add_argument(
        "--install",
        action="store_true",
        help="Explicitly authorize staging and installing firmware",
    )
    args = parser.parse_args()
    package = Package.load(args.package)
    if args.inspect == args.install:
        parser.error("Choose exactly one of --inspect or --install")
    if (
        not args.inspect
        and args.target != "right"
        and package.manifest["profile"] not in ("debug", "vial")
    ):
        parser.error(
            "Left updates require debug or Vial updater firmware so completion can be verified over USB"
        )
    link = UsbLink()
    try:
        updater = Updater(link)
        targets = (1, 0) if args.target == "both" else (int(args.target == "right"),)
        if args.inspect:
            for target in targets:
                print(f"Target {target}: {updater.inspect(target, package)}")
        elif args.target == "both":
            updater.install_both(package)
        else:
            updater.install(targets[0], package)
    finally:
        link.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError, KeyError) as error:
        sys.exit(str(error))

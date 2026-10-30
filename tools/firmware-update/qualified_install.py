"""Freeze one target offline, qualify both halves, INSTALL once, observe return."""

import argparse
import hashlib
import json
import math
import os
import re
import secrets
import struct
import sys
import time
from collections.abc import Callable
from pathlib import Path

import read_health
import studio_rpc
import update
from read_health import Health, HealthReader
from studio_rpc import JsonSchema, JsonValue
from update import ZMK_BOOT_CRC, Link, Op, Package, Reply, Updater, UsbLink


class QualifiedInstall:
    def __init__(self, case: Path, link_factory: Callable[[], Link] = UsbLink) -> None:
        self._case = case.resolve()
        self._link_factory = link_factory

    @staticmethod
    def _runtime() -> dict[str, Path]:
        return {
            "qualified_install.py": Path(__file__),
            "update.py": Path(update.__file__),
            "read_health.py": Path(read_health.__file__),
            "studio_rpc.py": Path(studio_rpc.__file__),
        }

    @staticmethod
    def _write(path: Path, data: bytes) -> None:
        with os.fdopen(
            os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb"
        ) as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())

    def _save(self, name: str, value: dict[str, JsonValue]) -> None:
        data = (json.dumps(value, indent=2) + "\n").encode()
        self._write(self._case / name, data)
        self._write(
            self._case / (name + ".sha256"), hashlib.sha256(data).hexdigest().encode()
        )
        if os.name != "nt":
            descriptor = os.open(self._case, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)

    def _read(self, name: str) -> dict[str, JsonValue]:
        data = (self._case / name).read_bytes()
        if (
            hashlib.sha256(data).hexdigest()
            != (self._case / (name + ".sha256")).read_text()
        ):
            raise ValueError("Case metadata or evidence changed")
        value: JsonValue = json.loads(data)
        return JsonSchema.mapping(value)

    @staticmethod
    def _number(value: JsonValue) -> float:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise TypeError("Expected qualification time")
        if not math.isfinite(value):
            raise ValueError("Qualification time must be finite")
        return float(value)

    @staticmethod
    def _before(
        document: dict[str, JsonValue],
    ) -> tuple[tuple[int, int], tuple[int, int]]:
        before = JsonSchema.sequence(document["before"])
        if len(before) != 2:
            raise ValueError("Exactly two before identities are required")
        result: list[tuple[int, int]] = []
        for target, raw in enumerate(before):
            pair = JsonSchema.sequence(raw)
            if len(pair) != 2:
                raise ValueError("Image identity requires size and CRC")
            size, crc = JsonSchema.uint(pair[0]), JsonSchema.uint(pair[1])
            if not 1024 <= size <= (0x1E000 if target else 0x1A000) or size % 4:
                raise ValueError("Invalid before image size")
            result.append((size, crc))
        return result[0], result[1]

    @staticmethod
    def _application(directory: Path) -> Package:
        package = Package.load(directory)
        if (
            package.manifest["profile"] not in ("zmk-studio", "zmk-studio-universal")
            or package.manifest["boot_crc"] != ZMK_BOOT_CRC
            or not package.image.endswith(b"GMK70ZMKBOOT\x01\0\0\0")
        ):
            raise ValueError(
                "Expected reviewed permanent Studio package and native footer"
            )
        return package

    @classmethod
    def create_case(
        cls,
        case: Path,
        package: Path,
        before_left: tuple[int, int],
        before_right: tuple[int, int],
        source_commit: str,
        target: int = 0,
    ) -> None:
        if re.fullmatch(r"[0-9a-fA-F]{40}", source_commit) is None:
            raise ValueError(
                "Source commit must contain exactly forty hexadecimal digits"
            )
        metadata: dict[str, JsonValue] = {
            "source_commit": source_commit.lower(),
            "target": target,
            "before": [
                [before_left[0], before_left[1]],
                [before_right[0], before_right[1]],
            ],
        }
        before = cls._before(metadata)
        candidate = cls._application(package)
        candidate.validate_target(target)
        if (len(candidate.image), candidate.manifest["app_crc"]) == before[target]:
            raise ValueError("Fresh case requires a distinct selected-target image")
        case.mkdir(mode=0o700)
        operator = cls(case)
        (case / "package").mkdir(mode=0o700)
        inputs = operator._runtime() | {
            "package/" + name: package / name
            for name in ("application.bin", "resident.bin", "update.json")
        }
        files: dict[str, JsonValue] = {}
        for name, source in inputs.items():
            data = source.read_bytes()
            operator._write(case / name, data)
            files[name] = hashlib.sha256(data).hexdigest()
        metadata["files"] = files
        operator._save("freeze.json", metadata)

    def _load(self) -> tuple[Package, dict[str, JsonValue], str]:
        metadata = self._read("freeze.json")
        files = JsonSchema.mapping(metadata["files"])
        runtime = self._runtime()
        expected = set(runtime) | {
            "package/" + name
            for name in ("application.bin", "resident.bin", "update.json")
        }
        if (
            set(files) != expected
            or re.fullmatch(r"[0-9a-f]{40}", JsonSchema.text(metadata["source_commit"]))
            is None
        ):
            raise ValueError("Invalid frozen case inputs")
        for name, raw_hash in files.items():
            expected_hash = JsonSchema.text(raw_hash)
            paths = [self._case / name] + ([runtime[name]] if name in runtime else [])
            if any(
                hashlib.sha256(path.read_bytes()).hexdigest() != expected_hash
                for path in paths
            ):
                raise ValueError("Frozen input changed")
        self._before(metadata)
        package = self._application(self._case / "package")
        target = self._target(metadata)
        package.validate_target(target)
        if (len(package.image), package.manifest["app_crc"]) == self._before(metadata)[
            target
        ]:
            raise ValueError("Candidate equals before selected-target identity")
        return (
            package,
            metadata,
            hashlib.sha256((self._case / "freeze.json").read_bytes()).hexdigest(),
        )

    @staticmethod
    def _target(metadata: dict[str, JsonValue]) -> int:
        target = JsonSchema.uint(metadata.get("target", 0))
        if target not in (0, 1):
            raise ValueError("Frozen target must be LEFT or RIGHT")
        return target

    @staticmethod
    def _identities(
        reader: HealthReader,
        expected: tuple[tuple[int, int], tuple[int, int]],
        *,
        sealed_info: Reply | None = None,
        install_target: int = 0,
        session: int = 0,
        received: int = 0,
    ) -> None:
        if sealed_info is not None and (
            sealed_info.operation != Op.INFO
            or sealed_info.result
            or sealed_info.state != 2
            or sealed_info.session != session
            or not session
            or sealed_info.received != received
        ):
            raise RuntimeError("Sealed selected-target updater context changed")
        for target in (0, 1):
            reply = (
                sealed_info
                if target == install_target and sealed_info is not None
                else reader.identity(target)
            )
            if (
                (reply.size, reply.crc) != expected[target]
                or reply.role != (0 if target else 3)
                or reply.state
                != (2 if target == install_target and sealed_info is not None else 0)
                or reply.abi != 1
                or reply.boot_crc != ZMK_BOOT_CRC
            ):
                raise RuntimeError("Image, physical role or resident identity differs")

    @staticmethod
    def _sample(reader: HealthReader, target: int) -> Health:
        sample = reader.sample(target, secrets.randbelow(0xFFFFFFFF) + 1)
        if (
            sample.flags not in (18, 22)
            or sample.feed_calls
            or sample.role != (0 if target else 3)
        ):
            raise RuntimeError("Permanent no-watchdog health policy differs")
        return sample

    @staticmethod
    def _clean(reader: HealthReader) -> None:
        if reader.transport_retries or reader.relay_retries:
            raise RuntimeError("Qualification requires zero transport or relay retries")

    @classmethod
    def _report(cls, proof: dict[str, JsonValue]) -> None:
        window = cls._number(proof["verified_window_seconds"])
        if (
            proof.get("read_only") is not True
            or window < 70
            or cls._number(proof["duration_seconds"]) < window
            or any(
                JsonSchema.uint(proof[key])
                for key in ("transport_retries", "relay_retries", "reconnects")
            )
        ):
            raise ValueError("Incomplete or retried both-half qualification")
        targets: set[int] = set()
        for raw in JsonSchema.sequence(proof["samples"]):
            sample = JsonSchema.mapping(raw)
            target = JsonSchema.uint(sample["target"])
            if (
                target not in (0, 1)
                or JsonSchema.uint(sample["flags"]) not in (18, 22)
                or JsonSchema.uint(sample["feed_calls"])
                or JsonSchema.uint(sample["role"]) != (0 if target else 3)
            ):
                raise ValueError("Qualification contains an invalid health sample")
            targets.add(target)
        if targets != {0, 1}:
            raise ValueError("Qualification must contain both halves")

    @classmethod
    def _age(cls, proof: dict[str, JsonValue]) -> float:
        elapsed = time.time() - cls._number(proof["completed_epoch_seconds"])
        if not 0 <= elapsed <= 300:
            raise ValueError("Both-half qualification expired")
        return elapsed

    @classmethod
    def _qualified(cls, reader: HealthReader, proof: dict[str, JsonValue]) -> None:
        for target in (0, 1):
            sample = cls._sample(reader, target)
            elapsed = cls._age(proof)
            delta = (
                sample.uptime_ms - JsonSchema.uint(proof[f"up{target}"])
            ) & 0xFFFFFFFF
            if abs(delta - elapsed * 1000) > 5000 + elapsed * 50:
                raise RuntimeError("Boot changed since qualification")
        cls._clean(reader)

    def baseline(self) -> None:
        self._observe(False)

    def observe(self) -> None:
        self._observe(True)

    def _observe(self, updated: bool) -> None:
        package, metadata, digest = self._load()
        name = "after-health.json" if updated else "baseline-health.json"
        if (self._case / name).exists():
            raise FileExistsError("Qualification evidence already exists")
        if updated and self._read("INSTALL-requested.json")["freeze_sha256"] != digest:
            raise ValueError("Install marker belongs to another case")
        before = self._before(metadata)
        target = self._target(metadata)
        expected = tuple(
            (len(package.image), package.manifest["app_crc"])
            if updated and side == target
            else before[side]
            for side in (0, 1)
        )
        expected_pair = (expected[0], expected[1])
        deadline = time.monotonic() + 80
        while True:
            link: Link | None = None
            try:
                link = self._link_factory()
                reader = HealthReader(link)
                self._identities(reader, expected_pair)
                if updated and time.monotonic() >= deadline:
                    raise TimeoutError(
                        "Image return exceeded the 80-second admission bound"
                    )
                break
            except (OSError, RuntimeError, TimeoutError):
                if link is not None:
                    link.close()
                if not updated:
                    raise
                if time.monotonic() >= deadline:
                    raise TimeoutError(
                        "No exact image/resident return; no writes or resets attempted"
                    ) from None
                time.sleep(0.5)
        try:
            value: JsonValue = json.loads(
                json.dumps(reader.observe((0, 1), duration=70))
            )
            proof = JsonSchema.mapping(value)
            self._report(proof)
            self._identities(reader, expected_pair)
            samples = [self._sample(reader, target) for target in (0, 1)]
            self._clean(reader)
            proof.update(
                {
                    "completed_epoch_seconds": time.time(),
                    "up0": samples[0].uptime_ms,
                    "up1": samples[1].uptime_ms,
                    "freeze_sha256": digest,
                }
            )
            self._save(name, proof)
            print(
                "Both-half 70-second qualification passed; exact images/residents, zero retries/feeds",
                flush=True,
            )
        finally:
            link.close()

    def install(self) -> None:
        package, metadata, digest = self._load()
        target = self._target(metadata)
        marker = self._case / "INSTALL-requested.json"
        if marker.exists():
            raise FileExistsError("INSTALL already requested; never replay")
        proof = self._read("baseline-health.json")
        self._report(proof)
        if proof["freeze_sha256"] != digest:
            raise ValueError("Baseline belongs to another frozen case")
        self._age(proof)
        link = self._link_factory()
        updater, reader = Updater(link), HealthReader(link)
        begun = False
        session = secrets.randbelow(0xFFFFFFFF) + 1
        try:
            self._identities(reader, self._before(metadata))
            self._qualified(reader, proof)
            updater.command(
                target,
                Op.BEGIN,
                session,
                data=struct.pack(
                    "<III", len(package.image), package.manifest["app_crc"], 1
                ),
            )
            begun = True
            for offset in range(0, len(package.image), 16):
                self._age(proof)
                data = package.image[offset : offset + 16]
                if updater.command(
                    target, Op.DATA, session, offset, data
                ).received != offset + len(data):
                    raise RuntimeError("Staged offset differs from exact image")
            seal = updater.command(target, Op.SEAL, session)
            if seal.state != 2 or (seal.size, seal.crc) != (
                len(package.image),
                package.manifest["app_crc"],
            ):
                raise RuntimeError("Sealed image identity differs")
            self._load()
            # Selected INFO retains its sealed state/session and running image.
            # Its peer must remain idle on the exact qualified application.
            self._identities(
                reader,
                self._before(metadata),
                sealed_info=updater.command(target, Op.INFO),
                install_target=target,
                session=session,
                received=len(package.image),
            )
            self._qualified(reader, proof)
            self._save(
                marker.name,
                {
                    "target": target,
                    "session": session,
                    "size": len(package.image),
                    "crc": package.manifest["app_crc"],
                    "install_requests": 1,
                    "freeze_sha256": digest,
                },
            )
            self._age(proof)
            reply = updater.request_install(target, session)
            if (
                reply.result
                or reply.operation != Op.INSTALL
                or reply.session != session
                or reply.state != 3
                or reply.role != (0 if target else 3)
                or reply.abi != 1
                or reply.boot_crc != 0
                or (reply.size, reply.crc)
                != (len(package.image), package.manifest["app_crc"])
            ):
                raise RuntimeError("INSTALL acknowledgement differs; never replay")
            self._save(
                "INSTALL-ack.json",
                {
                    "target": target,
                    "session": session,
                    "size": reply.size,
                    "crc": reply.crc,
                    "acknowledged": True,
                },
            )
            print(
                f"One {'RIGHT' if target else 'LEFT'} INSTALL acknowledged; independent read-only observation required",
                flush=True,
            )
        finally:
            try:
                if begun and not marker.exists():
                    updater.command(target, Op.ABORT, session)
            finally:
                link.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action", choices=("create-case", "baseline", "install", "observe")
    )
    parser.add_argument("--case", type=Path, required=True)
    parser.add_argument("--package", type=Path)
    parser.add_argument("--source-commit")
    parser.add_argument("--before-left", type=int, nargs=2, metavar=("SIZE", "CRC"))
    parser.add_argument("--before-right", type=int, nargs=2, metavar=("SIZE", "CRC"))
    parser.add_argument("--target", choices=("left", "right"))
    args = parser.parse_args()
    case: Path = args.case
    if args.action == "create-case":
        package: Path | None = args.package
        commit: str | None = args.source_commit
        left: list[int] | None = args.before_left
        right: list[int] | None = args.before_right
        if package is None or commit is None or left is None or right is None:
            parser.error(
                "create-case requires package, source commit and both before size/CRC pairs"
            )
        QualifiedInstall.create_case(
            case,
            package,
            (left[0], left[1]),
            (right[0], right[1]),
            commit,
            target=int(args.target == "right"),
        )
        print("Offline frozen single-target case created; no device accessed")
    else:
        if any(
            value is not None
            for value in (
                args.package,
                args.source_commit,
                args.before_left,
                args.before_right,
                args.target,
            )
        ):
            parser.error("Device operations accept only their frozen --case")
        operator = QualifiedInstall(case)
        if args.action == "install":
            operator.install()
        elif args.action == "baseline":
            operator.baseline()
        else:
            operator.observe()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, TypeError) as error:
        sys.exit(str(error))

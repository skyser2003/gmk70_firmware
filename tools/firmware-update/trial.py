"""Offline staging-trial packages and upload plans. No USB/HID or RUN operations."""

import argparse
import hashlib
import json
import struct
import zlib
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import TypedDict

STAGE = 0x08020000
RAM_START = 0x20000040
RAM_END = 0x20006FC0


class TrialManifest(TypedDict):
    format: str
    board: str
    mcu: str
    target: str
    load_address: int
    ram_start: int
    ram_end: int
    size: int
    crc32: int
    sha256: str
    recovery: str


class TrialOp(IntEnum):
    BEGIN = 2
    DATA = 3
    SEAL = 4


@dataclass(frozen=True)
class TrialPackage:
    image: bytes
    manifest: TrialManifest

    @classmethod
    def create(cls, image: bytes, target: str) -> "TrialPackage":
        if target not in ("left", "right"):
            raise ValueError("A trial package must identify one half")
        if not 1024 <= len(image) <= 0x1E000 or len(image) % 4:
            raise ValueError("Trial size must be 1024..122880 bytes, multiple of four")
        stack, *vectors = struct.unpack_from("<16I", image)
        if not RAM_START < stack <= RAM_END or stack % 8:
            raise ValueError("Trial stack overlaps reserved RAM or is unaligned")
        for index, vector in enumerate(vectors):
            if vector == 0 and index > 2:
                continue
            if (
                not vector & 1
                or vector < STAGE + 64
                or vector & ~1 > STAGE + len(image) - 2
            ):
                raise ValueError("Trial core vectors must point inside staging code")
        return cls(
            image,
            {
                "format": "gmk70-staging-trial-v2",
                "board": "zuoya/gmk70",
                "mcu": "WB32FQ95-256K",
                "target": target,
                "load_address": STAGE,
                "ram_start": RAM_START,
                "ram_end": RAM_END,
                "size": len(image),
                "crc32": zlib.crc32(image),
                "sha256": hashlib.sha256(image).hexdigest(),
                "recovery": "no-feed-iwdg-v1",
            },
        )

    @classmethod
    def load(cls, directory: Path) -> "TrialPackage":
        raw: object = json.loads((directory / "trial.json").read_text())
        if not isinstance(raw, dict) or not isinstance(raw.get("target"), str):
            raise TypeError("Invalid trial manifest")
        package = cls.create((directory / "trial.bin").read_bytes(), raw["target"])
        expected: dict[str, object] = dict(package.manifest)
        if raw != expected or any(
            type(raw[key]) is not type(value) for key, value in expected.items()
        ):
            raise ValueError(
                "Invalid identity, memory contract or corrupt trial package"
            )
        return package

    def write(self, directory: Path) -> None:
        if self != self.create(self.image, self.manifest["target"]):
            raise ValueError("Invalid trial package")
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "trial.bin").write_bytes(self.image)
        (directory / "trial.json").write_text(
            json.dumps(self.manifest, indent=2) + "\n"
        )


def upload_packets(package: TrialPackage, session: int) -> tuple[bytes, ...]:
    """Produce a plan only; the final operation is SEAL, never RUN/INSTALL."""
    if type(session) is not int or not 0 < session < 2**32:
        raise ValueError("Trial session must be a nonzero uint32")
    if package != TrialPackage.create(package.image, package.manifest["target"]):
        raise ValueError("Invalid trial package")
    target = 0 if package.manifest["target"] == "left" else 1

    def packet(op: TrialOp, offset: int = 0, data: bytes = b"") -> bytes:
        return (
            bytes((0x7F, 0x71, 2, target, op, len(data), 0, 0))
            + struct.pack("<II", session, offset)
            + data.ljust(16, b"\0")
        )

    begin = struct.pack(
        "<IIII", len(package.image), package.manifest["crc32"], STAGE, RAM_END
    )
    chunks = (
        packet(TrialOp.DATA, offset, package.image[offset : offset + 16])
        for offset in range(0, len(package.image), 16)
    )
    return (packet(TrialOp.BEGIN, data=begin), *chunks, packet(TrialOp.SEAL))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("package")
    pack.add_argument("binary", type=Path)
    pack.add_argument("--target", choices=("left", "right"), required=True)
    pack.add_argument("--output", type=Path, required=True)
    inspect = commands.add_parser("inspect")
    inspect.add_argument("directory", type=Path)
    plan = commands.add_parser("plan")
    plan.add_argument("directory", type=Path)
    plan.add_argument("--session", type=int, required=True)
    args = parser.parse_args()
    if args.command == "package":
        TrialPackage.create(args.binary.read_bytes(), args.target).write(args.output)
    elif args.command == "inspect":
        print(json.dumps(TrialPackage.load(args.directory).manifest, indent=2))
    else:
        package = TrialPackage.load(args.directory)
        print(
            json.dumps(
                {
                    "offline_only": True,
                    "packets": [p.hex() for p in upload_packets(package, args.session)],
                },
                indent=2,
            )
        )


if __name__ == "__main__":
    main()

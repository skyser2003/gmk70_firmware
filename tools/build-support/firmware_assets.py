"""The public firmware-only release format, shared by builds and repackaging."""

import hashlib
import shutil
import zipfile
from pathlib import Path

UPDATE_FILES = ("application.bin", "install.bin", "resident.bin", "update.json")
INNER_CHECKSUMS = "FIRMWARE-SHA256SUMS"


def asset_stem(variant: str) -> str:
    return "gmk70-" + (
        "vial-unlocked" if variant == "vial-unlocked-updater" else variant
    )


def is_updater(variant: str) -> bool:
    return variant == "zmk" or variant.endswith("-updater")


def image_name(variant: str) -> str:
    if variant == "zmk-standalone":
        return "zmk.bin"
    keymap = (
        "vial"
        if variant == "vial"
        else variant.removeprefix("via-").removeprefix("qmk-")
    )
    return f"zuoya_gmk70_{keymap}.bin"


def asset_name(variant: str) -> str:
    return asset_stem(variant) + (".zip" if is_updater(variant) else ".bin")


def checksum(data: bytes, name: str) -> str:
    return f"{hashlib.sha256(data).hexdigest()}  {name}\n"


def write_package(source: Path, destination: Path, variant: str) -> None:
    """Copy unchanged firmware bytes; include only the updater's required metadata."""
    name = asset_name(variant)
    output = destination / name
    if is_updater(variant):
        manifest = ""
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for member in UPDATE_FILES:
                data = (source / member).read_bytes()
                archive.writestr(member, data)
                manifest += checksum(data, member)
            archive.writestr(INNER_CHECKSUMS, manifest)
    else:
        shutil.copyfile(source / image_name(variant), output)
    (destination / f"{asset_stem(variant)}.sha256").write_text(
        checksum(output.read_bytes(), name)
    )


def verify_package(path: Path, variant: str) -> None:
    if not is_updater(variant):
        return
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or set(names) != {
            *UPDATE_FILES,
            INNER_CHECKSUMS,
        }:
            raise ValueError(
                "Updater ZIP must contain only firmware, update.json and checksums"
            )
        if any(item.file_size > 1024 * 1024 for item in archive.infolist()):
            raise ValueError("Oversized updater package member")
        expected = "".join(checksum(archive.read(name), name) for name in UPDATE_FILES)
        if archive.read(INNER_CHECKSUMS).decode() != expected:
            raise ValueError("Updater ZIP checksum mismatch")

"""Build and package the supported GMK70 distributions for GitHub Actions."""

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path
from typing import TypedDict, cast

from firmware_assets import asset_stem, write_package

ROOT = Path(__file__).resolve().parents[2]
VARIANTS = {
    "qmk-default": ("qmk", "default", "normal"),
    "via-release": ("qmk", "release", "normal"),
    "via-debug": ("qmk", "debug", "normal"),
    "via-release-updater": ("qmk", "release", "updater"),
    "via-debug-updater": ("qmk", "debug", "updater"),
    "vial": ("vial", "vial", "normal"),
    "vial-updater": ("vial", "vial", "updater"),
    "vial-unlocked-updater": ("vial", "vial", "unlocked-updater"),
    "zmk": ("zmk", "wired-studio-updater", "normal"),
    "zmk-standalone": ("zmk", "wired-studio-standalone", "normal"),
}


class ZmkReport(TypedDict):
    board: str
    variant: str
    runtime_roles: list[str]
    sha256: str
    bytes: int
    main_wrapper: str
    trial_backend: str
    updater: str


def verify_zmk_report(source: Path, variant: str) -> None:
    report = cast(ZmkReport, json.loads((source / "image-check.json").read_text()))
    standalone = variant == "wired-studio-standalone"
    application = (
        source / ("zmk.bin" if standalone else "application.bin")
    ).read_bytes()
    if (
        report.get("board") != "gmk70_wired"
        or report.get("variant") != variant
        or report.get("updater") != ("absent" if standalone else "present")
        or report.get("runtime_roles") != ["left", "right"]
        or report.get("trial_backend") != "absent"
        or report.get("main_wrapper") != "absent"
        or report.get("bytes") != len(application)
        or report.get("sha256") != hashlib.sha256(application).hexdigest()
        or application != (source / "zmk.bin").read_bytes()
    ):
        raise ValueError(
            "ZMK release requires the validated universal Studio application"
        )


def package_variant(root: Path, variant: str) -> None:
    platform, keymap, mode = VARIANTS[variant]
    folder = keymap + (f"-{mode}" if mode != "normal" else "")
    source = root / "artifacts" / platform / folder
    target = f"zuoya_gmk70_{keymap}"
    stem = asset_stem(variant)
    manifest = source / "SHA256SUMS"
    if manifest.is_symlink():
        raise ValueError(f"Redirected checksum manifest: {manifest}")
    files: set[str] = set()
    for line in manifest.read_text().splitlines():
        entry = re.fullmatch(r"([0-9a-f]{64}) [ *]([A-Za-z0-9][A-Za-z0-9_.-]*)", line)
        if entry is None or entry[2] in files:
            raise ValueError(f"Invalid checksum entry: {line}")
        digest, name = entry.groups()
        if name.endswith(".hex"):
            raise ValueError(
                "HEX firmware is no longer distributed; rebuild this variant"
            )
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Missing or redirected artifact: {name}")
        with path.open("rb") as handle:
            if hashlib.file_digest(handle, "sha256").hexdigest() != digest:
                raise ValueError(f"Checksum mismatch: {name}")
        files.add(name)
    required = {"sources.lock.json", "build-info.txt"}
    if platform == "zmk":
        required.update(
            {
                "zmk.bin",
                "zmk.elf",
                "zmk.map",
                "zmk.config",
                "zephyr.dts",
                "compile_commands.json",
                "image-check.json",
            }
        )
        updater_files = {
            "package-info.json",
            "application.bin",
            "resident.bin",
            "install.bin",
            "update.json",
        }
        if variant == "zmk-standalone":
            if files & updater_files:
                raise ValueError("Standalone ZMK cannot contain updater artifacts")
        else:
            required.update(updater_files)
    else:
        required.update(
            {
                f"{target}.elf",
                f"{target}.map",
                "gmk70-source-origins.json",
                "cflags.txt",
                "ldflags.txt",
                "vial.json" if platform == "vial" else "ZUOYA_GMK70.json",
            }
        )
    if platform != "zmk" and mode != "normal":
        required.update(
            {
                "application.bin",
                "resident.bin",
                "resident.elf",
                "resident.map",
                "install.bin",
                "update.json",
            }
        )
    elif platform != "zmk":
        required.add(f"{target}.bin")
    if missing := required - files:
        raise ValueError(f"Missing required artifacts: {', '.join(sorted(missing))}")
    if platform == "zmk":
        verify_zmk_report(source, keymap)
    destination = root / "dist"
    destination.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{stem}-", dir=destination) as temporary:
        staging = Path(temporary)
        write_package(source, staging, variant)
        # Clean only this variant's old outputs; other family assets are untouched.
        for old_stem in {stem, f"gmk70-{variant}"}:
            for suffix in (".tar.gz", ".hex", ".bin", ".zip", ".sha256"):
                (destination / f"{old_stem}{suffix}").unlink(missing_ok=True)
        for path in staging.iterdir():
            path.replace(destination / path.name)
    print(f"Packaged {variant} in {destination}")


def build_variant(root: Path, variant: str) -> None:
    command = (
        ["bash", "scripts/build_zmk.sh", VARIANTS[variant][1]]
        if VARIANTS[variant][0] == "zmk"
        else ["bash", "scripts/build.sh", *VARIANTS[variant]]
    )
    subprocess.run(command, cwd=root, check=True)
    package_variant(root, variant)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("variant", nargs="?", default="all", choices=("all", *VARIANTS))
    parser.add_argument("--package-only", action="store_true")
    args = parser.parse_args()
    try:
        for variant in VARIANTS if args.variant == "all" else (args.variant,):
            if args.package_only:
                package_variant(ROOT, variant)
            else:
                build_variant(ROOT, variant)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Firmware packaging failed: {error}\n")


if __name__ == "__main__":
    main()

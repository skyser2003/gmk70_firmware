"""Build and package the supported GMK70 distributions for GitHub Actions."""

import argparse
import hashlib
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

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
}


def package_variant(root: Path, variant: str) -> None:
    platform, keymap, mode = VARIANTS[variant]
    folder = keymap + (f"-{mode}" if mode != "normal" else "")
    source = root / "artifacts" / platform / folder
    target = f"zuoya_gmk70_{keymap}"
    stem = f"gmk70-{variant}"
    manifest = source / "SHA256SUMS"
    if manifest.is_symlink():
        raise ValueError(f"Redirected checksum manifest: {manifest}")
    files: set[str] = set()
    for line in manifest.read_text().splitlines():
        entry = re.fullmatch(r"([0-9a-f]{64}) [ *]([A-Za-z0-9][A-Za-z0-9_.-]*)", line)
        if entry is None or entry[2] in files:
            raise ValueError(f"Invalid checksum entry: {line}")
        digest, name = entry.groups()
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Missing or redirected artifact: {name}")
        with path.open("rb") as handle:
            if hashlib.file_digest(handle, "sha256").hexdigest() != digest:
                raise ValueError(f"Checksum mismatch: {name}")
        files.add(name)
    required = {
        f"{target}.elf",
        f"{target}.map",
        "source.tar.gz",
        "sources.lock.json",
        "gmk70-source-origins.json",
        "build-info.txt",
        "cflags.txt",
        "ldflags.txt",
        "vial.json" if platform == "vial" else "ZUOYA_GMK70.json",
    }
    if mode != "normal":
        required.update(
            {
                "application.bin",
                "application.hex",
                "resident.bin",
                "resident.elf",
                "resident.map",
                "install.bin",
                "install.hex",
                "update.json",
            }
        )
    else:
        required.update({f"{target}.bin", f"{target}.hex"})
    if platform == "vial":
        required.update({f"{stem}.zip", "README.txt", "FIRMWARE-SHA256SUMS"})
    if missing := required - files:
        raise ValueError(f"Missing required artifacts: {', '.join(sorted(missing))}")
    files.add("SHA256SUMS")
    if (source / "build.log").is_file() and not (source / "build.log").is_symlink():
        files.add("build.log")
    destination = root / "dist"
    destination.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{stem}-", dir=destination) as temporary:
        staging = Path(temporary)
        archive = staging / f"{stem}.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            for name in sorted(files):
                bundle.add(source / name, arcname=name, recursive=False)
        if mode == "normal":
            for extension in ("bin", "hex"):
                shutil.copyfile(
                    source / f"{target}.{extension}", staging / f"{stem}.{extension}"
                )
        if platform == "vial":
            shutil.copyfile(source / f"{stem}.zip", staging / f"{stem}.zip")
        checksums = []
        for path in sorted(staging.iterdir()):
            with path.open("rb") as handle:
                checksums.append(
                    f"{hashlib.file_digest(handle, 'sha256').hexdigest()}  {path.name}\n"
                )
        (staging / f"{stem}.sha256").write_text("".join(checksums))
        for path in staging.iterdir():
            path.replace(destination / path.name)
    print(f"Packaged {variant} in {destination}")


def build_variant(root: Path, variant: str) -> None:
    subprocess.run(
        ["bash", "scripts/build.sh", *VARIANTS[variant]], cwd=root, check=True
    )
    package_variant(root, variant)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("variant", nargs="?", default="all", choices=("all", *VARIANTS))
    args = parser.parse_args()
    try:
        for variant in VARIANTS if args.variant == "all" else (args.variant,):
            build_variant(ROOT, variant)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Firmware packaging failed: {error}\n")


if __name__ == "__main__":
    main()

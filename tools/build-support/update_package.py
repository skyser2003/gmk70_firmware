"""Build the resident WB32 installer and package an opt-in QMK application."""

import argparse
import hashlib
import json
import shlex
import subprocess
import zlib
from pathlib import Path

BOOT_SIZE = 0x2000
APP_ADDRESS = 0x08002000
APP_SIZE = 0x1E000
FLASH_SIZE = 0x40000


def build(framework_root: Path, output: Path, profile: str) -> None:
    output = output.resolve()
    source = (
        Path(__file__).resolve().parents[2]
        / "firmware/qmk-common/keyboards/zuoya/gmk70/update"
    )
    flags = [
        flag
        for flag in shlex.split((output / "cflags.txt").read_text())
        if flag != "-fshort-wchar"
    ]
    flags.extend(["-DGMK70_BOOT_STAGE", "-ffreestanding", "-fno-builtin"])
    objects: list[str] = []
    for name, path in (
        ("boot", source / "boot.c"),
        ("image", source / "image.c"),
        ("flash", source / "wb32_flash.c"),
        (
            "vendor_flash",
            framework_root
            / "lib/chibios-contrib/os/hal/ports/WB32/WB32FQ95xx/hal_efl_lld.c",
        ),
    ):
        destination = output / f"resident-{name}.o"
        subprocess.run(
            ["arm-none-eabi-gcc", *flags, "-c", str(path), "-o", str(destination)],
            cwd=framework_root,
            check=True,
        )
        objects.append(str(destination))
    elf = output / "resident.elf"
    subprocess.run(
        [
            "arm-none-eabi-gcc",
            "-mcpu=cortex-m3",
            "-mthumb",
            "-nostdlib",
            "-Wl,--gc-sections",
            f"-Wl,-Map={output / 'resident.map'}",
            "-T",
            str(source / "boot.ld"),
            *objects,
            "-lc",
            "-lgcc",
            "-o",
            str(elf),
        ],
        check=True,
    )
    subprocess.run(
        [
            "arm-none-eabi-objcopy",
            "-O",
            "binary",
            str(elf),
            str(output / "resident.bin"),
        ],
        check=True,
    )
    for object_path in objects:
        Path(object_path).unlink()
    for extension in ("bin", "hex"):
        (output / f"zuoya_gmk70_{profile}.{extension}").replace(
            output / f"application.{extension}"
        )
    application = (output / "application.bin").read_bytes()
    resident = (output / "resident.bin").read_bytes()
    symbols = subprocess.check_output(
        ["arm-none-eabi-nm", str(output / f"zuoya_gmk70_{profile}.elf")], text=True
    ).splitlines()
    image_end = next(
        int(line.split()[0], 16)
        for line in symbols
        if line.split()[-1] == "__flash0_free__"
    )
    if image_end - APP_ADDRESS != len(application):
        raise ValueError("Running-image checksum range does not match application.bin")
    if subprocess.check_output(["arm-none-eabi-nm", "-u", str(elf)], text=True).strip():
        raise ValueError("Resident installer has unresolved dependencies")
    if not 1024 <= len(application) <= APP_SIZE or len(application) % 4:
        raise ValueError("Application does not fit the updater partition")
    stack = int.from_bytes(application[:4], "little")
    reset = int.from_bytes(application[4:8], "little")
    if not (0x20000000 < stack <= 0x20007000 and stack % 8 == 0):
        raise ValueError("Unexpected application stack pointer")
    if not (reset & 1 and APP_ADDRESS <= reset < APP_ADDRESS + len(application)):
        raise ValueError("Application was not linked at the updater address")
    if not 64 <= len(resident) <= BOOT_SIZE:
        raise ValueError("Resident installer does not fit its immutable partition")
    boot = resident.ljust(BOOT_SIZE, b"\xff")
    initial = (boot + application).ljust(FLASH_SIZE, b"\xff")
    (output / "install.bin").write_bytes(initial)
    subprocess.run(
        [
            "arm-none-eabi-objcopy",
            "-I",
            "binary",
            "-O",
            "ihex",
            "--change-addresses=0x08000000",
            str(output / "install.bin"),
            str(output / "install.hex"),
        ],
        check=True,
    )
    manifest = {
        "format": 1,
        "board": "zuoya/gmk70",
        "mcu": "WB32FQ95-256K",
        "profile": profile,
        "app_address": APP_ADDRESS,
        "app_size": len(application),
        "app_crc": zlib.crc32(application),
        "app_sha256": hashlib.sha256(application).hexdigest(),
        "boot_crc": zlib.crc32(boot),
        "boot_sha256": hashlib.sha256(boot).hexdigest(),
        "install_sha256": hashlib.sha256(initial).hexdigest(),
    }
    (output / "update.json").write_text(json.dumps(manifest, indent=2) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("framework_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("profile", choices=("release", "debug", "vial"))
    args = parser.parse_args()
    build(args.framework_root.resolve(), args.output, args.profile)


if __name__ == "__main__":
    main()

"""Validate a real ZMK ARM ELF against the resident staging and retained RAM contract."""

import argparse
import json
import struct
import subprocess
from pathlib import Path

FLASH_START = 0x08020000
FLASH_END = FLASH_START + 120 * 1024
RAM_START = 0x20000040
RAM_END = 0x20006FC0


def validate_vectors(image: bytes) -> dict[str, str | int]:
    if len(image) < 8 or len(image) > 120 * 1024:
        raise ValueError("Staging image must contain vectors and fit within 120 KiB")
    stack, reset = struct.unpack_from("<II", image)
    if not RAM_START < stack <= RAM_END or stack % 8:
        raise ValueError("Initial stack overlaps reserved or unavailable RAM")
    if not reset & 1 or not FLASH_START <= reset & ~1 < FLASH_START + len(image):
        raise ValueError("Reset vector is outside the staging image")
    return {"stack": f"0x{stack:08x}", "reset": f"0x{reset:08x}", "bytes": len(image)}


def validate_elf(data: bytes, reset: int) -> None:
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Expected a little endian ELF32 image")
    machine = struct.unpack_from("<H", data, 18)[0]
    entry, phoff = struct.unpack_from("<II", data, 24)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    if phentsize < 32 or phoff + phentsize * phnum > len(data):
        raise ValueError("Truncated ELF program headers")
    if machine != 40 or entry != reset:
        raise ValueError("ELF must target ARM with its entry at the reset vector")
    for index in range(phnum):
        offset = phoff + phentsize * index
        kind, _, address, load, filesz, memsz = struct.unpack_from("<6I", data, offset)
        if kind != 1 or not memsz:
            continue
        flash = FLASH_START <= address and address + memsz <= FLASH_END
        ram = RAM_START <= address and address + memsz <= RAM_END
        if not flash and not ram:
            raise ValueError(
                f"ELF load segment overlaps protected memory at 0x{address:08x}"
            )
        if filesz and not FLASH_START <= load <= load + filesz <= FLASH_END:
            raise ValueError("ELF initialized data loads from outside staging")


def check(elf: Path, image: Path) -> dict[str, str | int]:
    result = validate_vectors(image.read_bytes())
    validate_elf(elf.read_bytes(), int(str(result["reset"]), 16))
    symbols = subprocess.check_output(
        ["arm-none-eabi-nm", "--defined-only", str(elf)], text=True
    )
    for name in (
        "zmk_keymap_position_state_changed",
        "zmk_usb_get_conn_state",
        "wb32_usb_clock_enable",
        "soc_early_init_hook",
    ):
        if not any(
            line.split()[-1] == name for line in symbols.splitlines() if line.split()
        ):
            raise ValueError(f"Missing actual ZMK/WB32 symbol {name}")
    result["application"] = "dependencies/zmk/app"
    result["board"] = "gmk70_left"
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.elf, args.image), indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"ZMK staging check failed: {error}\n")


if __name__ == "__main__":
    main()

"""Offline experimental permanent-ZMK packaging. Never builds a resident or opens HID."""

import argparse
import hashlib
import json
import zlib
from pathlib import Path

from update import ZMK_BOOT_CRC, ZMK_BOOT_SHA256, Manifest, Package


def build(image: Path, resident: Path, report: Path, output: Path) -> None:
    payload = image.read_bytes()
    raw = resident.read_bytes()
    boot = raw.ljust(0x2000, b"\xff")
    checked = json.loads(report.read_text())
    if (
        not isinstance(checked, dict)
        or checked.get("board") != "gmk70_wired"
        or checked.get("variant")
        not in ("wired-updater", "wired-update-test", "wired-studio-updater")
        or checked.get("load_address") != 0x08002000
        or checked.get("bytes") != len(payload)
        or checked.get("image_end") != 0x08002000 + len(payload)
        or checked.get("sha256") != hashlib.sha256(payload).hexdigest()
        or checked.get("main_wrapper") != "absent"
        or checked.get("trial_backend") != "absent"
    ):
        raise ValueError(
            "Package requires the matching validated permanent ZMK ELF/image report"
        )
    if (
        not 64 <= len(raw) <= 0x2000
        or hashlib.sha256(boot).hexdigest() != ZMK_BOOT_SHA256
        or zlib.crc32(boot) != ZMK_BOOT_CRC
    ):
        raise ValueError(
            "Missing or wrong pinned resident prerequisite; never rebuild it during ZMK packaging"
        )
    manifest: Manifest = {
        "format": 1,
        "board": "zuoya/gmk70",
        "mcu": "WB32FQ95-256K",
        "profile": "zmk-studio"
        if checked["variant"] == "wired-studio-updater"
        else "zmk",
        "app_address": 0x08002000,
        "app_size": len(payload),
        "app_crc": zlib.crc32(payload),
        "app_sha256": hashlib.sha256(payload).hexdigest(),
        "boot_crc": ZMK_BOOT_CRC,
        "boot_sha256": ZMK_BOOT_SHA256,
        "install_sha256": hashlib.sha256(
            (boot + payload).ljust(0x40000, b"\xff")
        ).hexdigest(),
    }
    Package(payload, manifest).validate_application()
    output.mkdir(parents=True, exist_ok=True)
    (output / "application.bin").write_bytes(payload)
    (output / "resident.bin").write_bytes(raw)
    (output / "update.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "package-info.json").write_text(
        json.dumps(
            {
                "development": "EXPERIMENTAL unlocked; not a normal release asset",
                "variant": checked["variant"],
                "resident_prerequisite": str(resident),
                "resident_padded_sha256": ZMK_BOOT_SHA256,
                "package": "application-only; no direct-USB installer generated",
            },
            indent=2,
        )
        + "\n"
    )
    Package.load(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--resident", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        build(args.image, args.resident, args.report, args.output)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"ZMK package rejected: {error}\n")


if __name__ == "__main__":
    main()

"""Validate a real ZMK ARM ELF against application/staging and retained RAM contracts."""

import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path
from typing import NamedTuple

FLASH_START = 0x08020000
FLASH_END = FLASH_START + 120 * 1024
RAM_START = 0x20000040
RAM_END = 0x20006FC0


def validate_radio_linking(
    symbols: set[str], disassembly: str, variant: str | None
) -> None:
    """Require qualified marker isolation and real linked radio call sites."""
    marker = "gmk_radio_test_marker"
    qualified = variant == "wired-studio-radio-qualification"
    if (marker in symbols) != qualified:
        raise ValueError(
            "Radio marker requires exactly the explicit qualification profile"
        )
    if "gmk_radio_poll" not in symbols:
        return
    for name in (
        "__wrap_zmk_usb_is_hid_ready",
        "__wrap_zmk_endpoint_get_selected",
    ):
        if name in symbols:
            raise ValueError(f"Radio image replaces native USB/Studio state via {name}")
    for name in (
        "__wrap_zmk_endpoint_send_report",
        "__wrap_zmk_hid_keyboard_clear",
        "__wrap_zmk_hid_consumer_clear",
        "__wrap_zmk_keymap_set_layer_binding_at_idx",
    ):
        if name not in symbols:
            raise ValueError(f"Missing linked radio wrapper {name}")
    functions = _function_bodies(disassembly)
    if not any(
        _calls(body, "__wrap_zmk_endpoint_send_report")
        for name, body in functions.items()
        if name.startswith("hid_listener")
    ):
        raise ValueError("HID listener bypasses the radio report wrapper")
    for name in ("__wrap_zmk_hid_keyboard_clear", "__wrap_zmk_hid_consumer_clear"):
        if not _calls(functions.get("zmk_endpoint_clear_reports", ""), name):
            raise ValueError(f"Endpoint clear bypasses radio wrapper {name}")


def _function_bodies(disassembly: str) -> dict[str, str]:
    parts = re.split(r"(?m)^[ \t]*[0-9a-fA-F]+ <([^>]+)>:\r?\n", disassembly)
    return dict(zip(parts[1::2], parts[2::2], strict=True))


class _ArmInstruction(NamedTuple):
    address: int
    opcode: str
    operands: str
    source: str

    def is_setup(self) -> bool:
        return (
            self.opcode
            in (
                "push",
                "mov",
                "movs",
                "movw",
                "movt",
                "ldr",
                "adr",
                "add",
                "adds",
                "sub",
                "subs",
                "nop",
            )
            and self.operands.split(",", 1)[0].strip() != "pc"
        )


def validate_studio_roles(disassembly: str) -> None:
    """Admit the pinned compiler's conservative CBZ-r0 role-guard pattern.

    LEFT falls straight through argument setup to the single mount. RIGHT
    skips past it; every subsequent local branch must stay past that call.
    Unsupported control flow is rejected rather than guessed about.
    """
    body = _function_bodies(disassembly).get("settings_backend_init", "")
    pattern = (
        r"^[ \t]*([0-9a-f]+):[ \t]+(?:[0-9a-f]{2,8}[ \t]+)+"
        r"([a-z][a-z0-9.]*)[ \t]*(.*)$"
    )
    instructions = [
        _ArmInstruction(
            int(match[1], 16), match[2].split(".")[0].lower(), match[3], line
        )
        for line in body.splitlines()
        if (match := re.fullmatch(pattern, line, re.IGNORECASE)) is not None
    ]
    roles = [
        i
        for i, inst in enumerate(instructions)
        if _calls(inst.source, "gmk_wired_is_left")
    ]
    mounts = [
        i
        for i, inst in enumerate(instructions)
        if _calls(inst.source, "gmk_settings_store_mount")
    ]
    if len(roles) != 1 or len(mounts) != 1 or roles[0] + 1 >= mounts[0]:
        raise ValueError("Universal Studio requires one guarded role probe and mount")
    role, mount = roles[0], mounts[0]
    addresses = {inst.address for inst in instructions}
    if len(addresses) != len(instructions) or [
        inst.address for inst in instructions
    ] != sorted(addresses):
        raise ValueError("Unsupported Studio instruction address order")
    guard = instructions[role + 1]
    target = re.fullmatch(
        r"r0,\s*([0-9a-f]+)(?:\s+<[^>]+>)?", guard.operands, re.IGNORECASE
    )
    if (
        guard.opcode != "cbz"
        or target is None
        or int(target[1], 16) not in addresses
        or int(target[1], 16) <= instructions[mount].address
        or any(
            not inst.is_setup()
            for inst in instructions[:role] + instructions[role + 2 : mount]
        )
    ):
        raise ValueError("Universal role false must skip mount; true must reach it")
    branches = {
        "b",
        "beq",
        "bne",
        "bcs",
        "bcc",
        "bmi",
        "bpl",
        "bvs",
        "bvc",
        "bhi",
        "bls",
        "bge",
        "blt",
        "bgt",
        "ble",
        "bal",
        "cbz",
        "cbnz",
    }
    for inst in instructions[role + 2 :]:
        if inst.opcode in branches:
            destination = re.fullmatch(
                r"([0-9a-f]+)(?:\s+<[^>]+>)?",
                inst.operands.rsplit(",", 1)[-1].strip(),
                re.IGNORECASE,
            )
            if (
                destination is None
                or int(destination[1], 16) not in addresses
                or int(destination[1], 16) <= instructions[mount].address
            ):
                raise ValueError(
                    "Peripheral path can re-enter or bypass the guarded mount"
                )
        elif (
            inst.opcode.startswith("it")
            or inst.opcode in ("tbb", "tbh", "bxj")
            or (inst.opcode == "bx" and inst.operands.strip() != "lr")
            or (
                inst.opcode == "blx"
                and re.fullmatch(r"[0-9a-f]+\s+<[^>]+>", inst.operands, re.IGNORECASE)
                is None
            )
            or inst.operands.split(",", 1)[0].strip() == "pc"
        ):
            raise ValueError("Unsupported indirect Studio role control flow")


def validate_flip_linking(symbols: set[str], disassembly: str) -> None:
    if "gmk_flip_poll" not in symbols:
        return
    wrapper = "__wrap_raise_zmk_keycode_state_changed"
    if wrapper not in symbols:
        raise ValueError("Missing linked function flip event wrapper")
    functions = _function_bodies(disassembly)
    if not any(
        _calls(body, wrapper)
        for name, body in functions.items()
        if name.startswith("raise_zmk_keycode_state_changed_from_encoded")
    ):
        raise ValueError("Native keycode behavior bypasses function flip")
    if not _calls(functions.get("worker", ""), "gmk_flip_poll"):
        raise ValueError("Function flip persistence bypasses the existing worker")


def _calls(body: str, target: str) -> bool:
    # Match an addressed instruction with encoded bytes. A symbol reference in
    # a comment, data load or unrelated function cannot establish a call site.
    instruction = (
        r"^[ \t]*[0-9a-f]+:[ \t]+(?:[0-9a-f]{2,8}[ \t]+)+"
        r"blx?(?:\.w)?[ \t]+[0-9a-f]+[ \t]+<"
        + re.escape(target)
        + r">[ \t]*(?:;[^\r\n]*)?$"
    )
    return re.search(instruction, body, re.MULTILINE | re.IGNORECASE) is not None


def validate_vectors(
    image: bytes, flash_start: int = FLASH_START
) -> dict[str, str | int]:
    if flash_start not in (0x08002000, FLASH_START):
        raise ValueError("Unsupported image slot")
    if len(image) < 8 or len(image) > 120 * 1024:
        raise ValueError("Staging image must contain vectors and fit within 120 KiB")
    stack, reset = struct.unpack_from("<II", image)
    if not RAM_START < stack <= RAM_END or stack % 8:
        raise ValueError("Initial stack overlaps reserved or unavailable RAM")
    if not reset & 1 or not flash_start <= reset & ~1 < flash_start + len(image):
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


def check(
    elf: Path, image: Path, board: str = "gmk70_left", variant: str | None = None
) -> dict[str, str | int | list[str]]:
    if board == "gmk70_wired":
        return check_wired(elf, image, variant)
    if board not in ("gmk70_left", "gmk70_right"):
        raise ValueError(f"Unsupported board {board}")
    result = validate_vectors(image.read_bytes())
    validate_elf(elf.read_bytes(), int(str(result["reset"]), 16))
    symbols = subprocess.check_output(
        ["arm-none-eabi-nm", "--defined-only", str(elf)], text=True
    )
    symbol_names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
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
    disassembly = ""
    if board == "gmk70_right" or "gmk_radio_poll" in symbol_names:
        disassembly = subprocess.check_output(
            ["arm-none-eabi-objdump", "-d", str(elf)], text=True
        )
    validate_radio_linking(symbol_names, disassembly, variant)
    if board == "gmk70_right":
        for name in ("main", "__wrap_main"):
            if not any(line.split()[-1] == name for line in symbols.splitlines()):
                raise ValueError(f"Missing right trial symbol {name}")
        validate_main_wrapping(disassembly)
        result["main_return"] = "successful real main + ready kscan + 500ms + SW reset"
    result["application"] = "dependencies/zmk/app"
    result["board"] = board
    return dict(result)


def validate_wired_elf(
    data: bytes, image: bytes, base: int, image_end: int, max_size: int = 0x1E000
) -> None:
    """Check the entire physical load image, including initialized SRAM data."""
    vectors = validate_vectors(image, base)
    if not 1024 <= len(image) <= max_size or len(image) % 4:
        raise ValueError("Wired image must satisfy updater size/alignment contract")
    for index, vector in enumerate(struct.unpack_from("<15I", image, 4)):
        if vector == 0 and index > 2:
            continue
        if not vector & 1 or not base + 64 <= vector & ~1 <= base + len(image) - 2:
            raise ValueError("Core vector points outside image code")
    if image_end != base + len(image):
        raise ValueError("Complete image-end does not match binary load range")
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Expected little endian ELF32")
    if struct.unpack_from("<H", data, 18)[0] != 40:
        raise ValueError("Expected ARM ELF")
    entry, phoff = struct.unpack_from("<II", data, 24)
    entsize, count = struct.unpack_from("<HH", data, 42)
    if (
        entry != int(str(vectors["reset"]), 16)
        or entsize < 32
        or phoff + entsize * count > len(data)
    ):
        raise ValueError("Wrong entry or truncated program headers")
    start, end = base + len(image), base
    shoff = struct.unpack_from("<I", data, 32)[0]
    shentsize, shnum = struct.unpack_from("<HH", data, 46)
    if shnum and (shentsize < 40 or shoff + shentsize * shnum > len(data)):
        raise ValueError("Truncated ELF section headers")
    sections: list[tuple[int, int, int]] = []
    matched: set[tuple[int, int, int]] = set()
    for index in range(shnum):
        _, kind, flags, address, offset, size = struct.unpack_from(
            "<6I", data, shoff + shentsize * index
        )
        if flags & 2 and kind != 8 and size:  # SHF_ALLOC, excluding SHT_NOBITS
            if offset + size > len(data):
                raise ValueError("Truncated initialized ELF section")
            sections.append((address, offset, size))
    for index in range(count):
        kind, offset, address, load, filesz, memsz = struct.unpack_from(
            "<6I", data, phoff + entsize * index
        )
        if kind != 1 or not memsz:
            continue
        if filesz > memsz or offset + filesz > len(data):
            raise ValueError("Truncated ELF load segment")
        if not (
            base <= address <= address + memsz <= base + max_size
            or RAM_START <= address <= address + memsz <= RAM_END
        ):
            raise ValueError("ELF segment overlaps protected memory")
        if filesz:
            if not base <= load < load + filesz <= base + len(image):
                raise ValueError("ELF initialized data loads outside image slot")
            if sections:
                # Zephyr objcopy fills inter-section alignment gaps with FF;
                # ELF program headers contain zero padding there. Compare all
                # actual initialized sections, never mistake padding for data.
                for section_address, section_offset, size in sections:
                    if address <= section_address < address + filesz:
                        relative = section_address - address
                        if (
                            relative + size > filesz
                            or data[section_offset : section_offset + size]
                            != image[
                                load - base + relative : load - base + relative + size
                            ]
                        ):
                            raise ValueError(
                                "ELF initialized section bytes differ from binary"
                            )
                        matched.add((section_address, section_offset, size))
            elif (
                data[offset : offset + filesz]
                != image[load - base : load - base + filesz]
            ):
                raise ValueError("ELF load bytes differ from binary")
            start, end = min(start, load), max(end, load + filesz)
    if start != base or end != image_end:
        raise ValueError("ELF load extent differs from complete image-end")
    if matched != set(sections):
        raise ValueError("Initialized ELF section is missing from the load segments")


def validate_handoff_payload(payload: bytes, enabled: bool) -> None:
    present = payload.endswith(b"GMK70ZMKBOOT\x01\0\0\0")
    if present != enabled:
        raise ValueError("Wrong or misplaced resident handoff capability footer")


def validate_permanent_symbols(symbols: set[str]) -> None:
    if any(
        name.startswith("gmk_zmk_watchdog_")
        or name in ("gmk_zmk_resident_handoff", "gmk_zmk_resident_branch")
        for name in symbols
    ):
        raise ValueError(
            "Permanent watchdog or outgoing resident handoff survived linking"
        )


def check_wired(
    elf: Path, image: Path, variant: str | None
) -> dict[str, str | int | list[str]]:
    if variant not in (
        "wired-updater",
        "wired-update-test",
        "wired-peripheral",
        "wired-qualification",
        "wired-studio-updater",
        "wired-studio-update-test",
        "wired-studio-radio-qualification",
        "wired-studio-handoff",
        "wired-studio-handoff-test",
        "wired-studio-watchdog-rollback",
        "wired-studio-watchdog-rollback-test",
    ):
        raise ValueError("Universal wired board requires explicit supported variant")
    qualification = variant == "wired-qualification"
    base = FLASH_START if qualification else 0x08002000
    payload = image.read_bytes()
    result: dict[str, str | int | list[str]] = dict(validate_vectors(payload, base))
    validate_handoff_payload(payload, not qualification)
    lines = subprocess.check_output(
        ["arm-none-eabi-nm", "--defined-only", str(elf)], text=True
    )
    symbols = {
        parts[-1]: int(parts[0], 16)
        for line in lines.splitlines()
        if len(parts := line.split()) == 3
    }
    for name in (
        "main",
        "__gmk_image_end",
        "__rom_region_start",
        "_flash_used",
        "_image_ram_end",
        "zmk_keymap_position_state_changed",
        "zmk_usb_get_conn_state",
        "wb32_usb_clock_enable",
        "soc_early_init_hook",
        "gmk_flash_page",
        "gmk_running_size",
        "gmk_zmk_receiver_step",
        "gmk_zmk_raw_hid_init",
        "gmk_wired_scan",
        "gmk_wired_update",
        "_k_thread_data_gmk_scan_thread",
        "_k_thread_data_gmk_serial_thread",
        "_k_thread_data_update_worker",
    ):
        if name not in symbols:
            raise ValueError(f"Missing native ZMK wired symbol {name}")
    validate_permanent_symbols(set(symbols))
    if "__wrap_main" in symbols:
        raise ValueError("Permanent/qualification wired target must not wrap main")
    if not qualification and "gmk_trial_packet_valid" not in symbols:
        raise ValueError("Missing peer trial validator")
    if qualification and "gmk_trial_packet_valid" in symbols:
        raise ValueError("Qualification retained peer trial code")
    if any(
        name.startswith("gmk_trial_") and name != "gmk_trial_packet_valid"
        for name in symbols
    ):
        raise ValueError("Common trial backend survived section garbage collection")
    if (
        symbols["__rom_region_start"] != base
        or symbols["__gmk_image_end"] != base + symbols["_flash_used"]
    ):
        raise ValueError("Wrong full-image checksum linker contract")
    validate_wired_elf(
        elf.read_bytes(),
        payload,
        base,
        symbols["__gmk_image_end"],
        0x1A000 if variant.startswith("wired-studio-") else 0x1E000,
    )
    disassembly = ""
    if variant.startswith("wired-studio-") or "gmk_radio_poll" in symbols:
        disassembly = subprocess.check_output(
            ["arm-none-eabi-objdump", "-d", str(elf)], text=True
        )
    validate_radio_linking(set(symbols), disassembly, variant)
    validate_flip_linking(set(symbols), disassembly)
    if variant.startswith("wired-studio-"):
        validate_studio_roles(disassembly)
        for name in (
            "settings_backend_init",
            "gmk_zmk_settings_poll",
            "gmk_zmk_settings_flash_page",
            "__wrap_usb_hid_register_device",
            "__wrap_zmk_studio_core_get_lock_state",
            "uart_rpc_interface_init",
        ):
            if name not in symbols:
                raise ValueError(f"Missing Studio integration symbol {name}")
        functions = _function_bodies(disassembly)
        if not re.search(
            r"\bblx?\s+[^\n]*<__wrap_usb_hid_register_device>",
            functions.get("zmk_usb_hid_init", ""),
        ):
            raise ValueError("Keyboard registration bypasses Studio Feature wrapper")
    result.update(
        application="dependencies/zmk/app",
        board="gmk70_wired",
        variant=variant,
        load_address=base,
        sha256=hashlib.sha256(payload).hexdigest(),
        image_end=symbols["__gmk_image_end"],
        ram_end=symbols["_image_ram_end"],
        development="EXPERIMENTAL unlocked",
        trial_backend="absent",
        main_wrapper="absent",
    )
    if variant.startswith("wired-studio-"):
        result["runtime_roles"] = ["left", "right"]
    return result


def validate_main_wrapping(disassembly: str) -> None:
    """Require startup to invoke the wrapper and wrapper to invoke actual ZMK main."""
    bodies = _function_bodies(disassembly)
    branch = r"\bblx?\s+[^\n]*"
    if not re.search(branch + r"<main>", bodies.get("__wrap_main", "")):
        raise ValueError("Right trial wrapper does not call actual main")
    if not re.search(branch + r"<__wrap_main>", bodies.get("bg_thread_main", "")):
        raise ValueError("Zephyr startup does not call right trial wrapper")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("image", type=Path)
    parser.add_argument(
        "--board",
        choices=("gmk70_left", "gmk70_right", "gmk70_wired"),
        default="gmk70_left",
    )
    parser.add_argument(
        "--variant",
        choices=(
            "wired-updater",
            "wired-update-test",
            "wired-peripheral",
            "wired-qualification",
            "wired-studio-updater",
            "wired-studio-update-test",
            "wired-studio-radio-qualification",
            "wired-studio-handoff",
            "wired-studio-handoff-test",
            "wired-studio-watchdog-rollback",
            "wired-studio-watchdog-rollback-test",
        ),
    )
    args = parser.parse_args()
    try:
        print(
            json.dumps(check(args.elf, args.image, args.board, args.variant), indent=2)
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"ZMK image check failed: {error}\n")


if __name__ == "__main__":
    main()

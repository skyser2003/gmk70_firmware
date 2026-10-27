"""Check the linked trial startup and retained-RAM contract without a device."""

import argparse
import re
import struct
import subprocess
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def check(elf: Path, *, runtime_watchdog: bool = False) -> None:
    def output(*arguments: str) -> str:
        return subprocess.check_output(arguments, text=True)

    symbols = {
        fields[-1]: (int(fields[0], 16), fields[-2])
        for line in output("arm-none-eabi-nm", "-S", str(elf)).splitlines()
        if len(fields := line.split()) in (3, 4)
    }

    def symbol(name: str) -> tuple[int, str]:
        require(name in symbols, f"Missing required symbol: {name}")
        return symbols[name]

    reset_address, reset_kind = symbol("Reset_Handler")
    symbol("gmk_trial_process")
    require(
        symbol("gmk_trial_record")[0] == 0x20000000, "Incorrect retained record base"
    )
    require(
        symbol("__ram0_base__")[0] == 0x20000040, "RAM must reserve the retained record"
    )
    require(symbol("__ram0_end__")[0] == 0x20007000, "Preserve bootloader magic")
    sections = output("arm-none-eabi-readelf", "-W", "-S", str(elf))
    retained = False
    for line in sections.splitlines():
        match = re.search(
            r"\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)",
            line,
        )
        if not match:
            continue
        name, kind, raw_address, raw_size = match.groups()
        address, size = int(raw_address, 16), int(raw_size, 16)
        if name == ".trial_retained":
            require(
                (kind, address, size) == ("NOBITS", 0x20000000, 64),
                "Incorrect retained record section: require 64 bytes of NOBITS at 0x20000000",
            )
            retained = True
        elif size and 0x20000000 <= address < 0x20009000:
            require(
                0x20000040 <= address and address + size <= 0x20007000,
                f"Section overlaps reserved RAM: {name}",
            )
    require(retained, "Missing retained record section")
    code = output(
        "arm-none-eabi-objdump", "-d", "--disassemble=Reset_Handler", str(elf)
    )
    if runtime_watchdog:
        require(reset_kind in ("W", "T"), "Runtime Reset_Handler must be upstream code")
        for forbidden in (
            "gmk_trial_early_boot",
            "gmk_trial_platform_handoff",
            "gmk_trial_role_read",
        ):
            require(
                not any(
                    name == forbidden or name.startswith(forbidden + ".")
                    for name in symbols
                ),
                f"Runtime profile contains forbidden startup symbol: {forbidden}",
            )
        crt_address = symbol("_crt0_entry")[0]
        if reset_address != crt_address:
            instructions = re.findall(
                r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2,8}\s+)+(.+?)\s*$", code, re.MULTILINE
            )
            require(
                len(instructions) == 1
                and re.fullmatch(
                    rf"b(?:\.w|\.n)?\s+0*{crt_address:x}\s+<_crt0_entry>",
                    instructions[0],
                )
                is not None,
                "Runtime Reset_Handler must branch directly to upstream _crt0_entry",
            )
        crt_code = output(
            "arm-none-eabi-objdump", "-d", "--disassemble=_crt0_entry", str(elf)
        )
        require(
            all(
                marker in crt_code
                for marker in ("cpsid", "MSP", "PSP", "CONTROL", "<__early_init>")
            ),
            "Runtime profile must retain normal ChibiOS CRT initialization",
        )
    else:
        require(reset_kind == "T", "Reset wrapper must be strong")
        masked = code.find("cpsid")
        early = code.find("<gmk_trial_early_boot>")
        crt = code.find("<_crt0_entry>")
        require(
            0 <= masked < early < crt,
            "Reset must mask interrupts before the pre-CRT hook",
        )
        require(
            "MSP" in code and "CONTROL" in code, "Reset must initialize MSP and CONTROL"
        )
        handoff = output(
            "arm-none-eabi-objdump",
            "-d",
            "--disassemble=gmk_trial_platform_handoff",
            str(elf),
        )
        require(
            "cpsid" in handoff and "cpsie" not in handoff,
            "Handoff must leave interrupts masked",
        )
    all_code = output("arm-none-eabi-objdump", "-d", str(elf))
    for wrapper in ("transport_master_if_connected", "transaction_rpc_exec"):
        require(
            re.search(rf"\bbl\s+[^\n]*<__wrap_{wrapper}>", all_code) is not None,
            f"Missing transport wrapper call: {wrapper}",
        )
    with tempfile.TemporaryDirectory() as directory:
        vectors = Path(directory) / "vectors.bin"
        subprocess.run(
            [
                "arm-none-eabi-objcopy",
                "--dump-section",
                f".vectors={vectors}",
                str(elf),
            ],
            check=True,
        )
        vector_data = vectors.read_bytes()
        require(len(vector_data) >= 8, "Missing stack/reset vectors")
        stack, reset = struct.unpack_from("<II", vector_data)
        require(
            stack == symbol("__main_stack_end__")[0], "Initial stack vector mismatch"
        )
        require(reset == reset_address | 1, "Reset vector must reference Reset_Handler")
    profile = (
        "runtime watchdog with normal startup"
        if runtime_watchdog
        else "strong pre-CRT reset hook"
    )
    print(f"Trial ELF: {profile} and retained RAM verified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--runtime-watchdog", action="store_true")
    args = parser.parse_args()
    try:
        check(args.elf, runtime_watchdog=args.runtime_watchdog)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Trial ELF check failed: {error}\n")

"""Check the linked trial startup and retained-RAM contract without a device."""

import argparse
import re
import struct
import subprocess
import tempfile
from pathlib import Path


def check(elf: Path) -> None:
    def output(*arguments: str) -> str:
        return subprocess.check_output(arguments, text=True)

    symbols = {
        fields[-1]: (int(fields[0], 16), fields[-2])
        for line in output("arm-none-eabi-nm", "-S", str(elf)).splitlines()
        if len(fields := line.split()) in (3, 4)
    }
    assert symbols["Reset_Handler"][1] == "T", "Reset wrapper must be strong"
    assert "gmk_trial_process" in symbols
    assert symbols["gmk_trial_record"][0] == 0x20000000
    assert symbols["__ram0_base__"][0] == 0x20000040
    assert symbols["__ram0_end__"][0] == 0x20007000, "Preserve bootloader magic"
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
            assert (kind, address, size) == ("NOBITS", 0x20000000, 64)
            retained = True
        elif size and 0x20000000 <= address < 0x20009000:
            assert 0x20000040 <= address and address + size <= 0x20007000, name
    assert retained, "Missing retained record section"
    code = output(
        "arm-none-eabi-objdump", "-d", "--disassemble=Reset_Handler", str(elf)
    )
    assert code.index("cpsid") < code.index("<gmk_trial_early_boot>")
    assert code.index("<gmk_trial_early_boot>") < code.index("<_crt0_entry>")
    assert "MSP" in code and "CONTROL" in code
    handoff = output(
        "arm-none-eabi-objdump",
        "-d",
        "--disassemble=gmk_trial_platform_handoff",
        str(elf),
    )
    assert "cpsid" in handoff and "cpsie" not in handoff
    all_code = output("arm-none-eabi-objdump", "-d", str(elf))
    for wrapper in ("transport_master_if_connected", "transaction_rpc_exec"):
        assert re.search(rf"\bbl\s+[^\n]*<__wrap_{wrapper}>", all_code), wrapper
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
        stack, reset = struct.unpack_from("<II", vectors.read_bytes())
        assert stack == symbols["__main_stack_end__"][0]
        assert reset == symbols["Reset_Handler"][0] | 1
    print("Trial ELF: strong pre-CRT reset hook and retained RAM verified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    check(parser.parse_args().elf)

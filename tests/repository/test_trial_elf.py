"""Exercise linked trial safety contracts through local binutils fixtures."""

import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
CHECKER = PROJECT / "tools/build-support/check_trial_elf.py"


class TrialElfTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.binaries = self.root / "bin"
        self.binaries.mkdir()
        # No ARM compiler is required: these tools supply binutils output so the
        # real CLI, including optimized Python, checks each linked contract.
        for name, body in {
            "nm": "print((root / 'symbols').read_text())",
            "readelf": "print((root / 'sections').read_text())",
            "objdump": """
name = next((arg.split('=', 1)[1] for arg in sys.argv if arg.startswith('--disassemble=')), 'all')
print((root / name).read_text())
""",
            "objcopy": """
destination = pathlib.Path(sys.argv[sys.argv.index('--dump-section') + 1].split('=', 1)[1])
destination.write_bytes((root / 'vectors').read_bytes())
""",
        }.items():
            tool = self.binaries / f"arm-none-eabi-{name}"
            tool.write_text(
                "#!/usr/bin/env python3\nimport pathlib, sys\n"
                "root = pathlib.Path(__file__).resolve().parents[1]\n" + body
            )
            tool.chmod(0o755)
        self.fixture(runtime=True)

    def fixture(self, runtime: bool) -> None:
        self.symbols = {
            "Reset_Handler": (0x08002000, "W" if runtime else "T"),
            "_crt0_entry": (0x08002100, "T"),
            "gmk_trial_process": (0x08002500, "T"),
            "gmk_trial_record": (0x20000000, "B"),
            "__ram0_base__": (0x20000040, "A"),
            "__ram0_end__": (0x20007000, "A"),
            "__main_stack_end__": (0x20001000, "A"),
        }
        self.sections = (
            "[ 1] .vectors PROGBITS 08002000 002000 000100 00 A 0 0 4\n"
            "[ 2] .trial_retained NOBITS 20000000 010000 000040 00 WA 0 0 4\n"
            "[ 3] .data PROGBITS 20000040 010040 000020 00 WA 0 0 4\n"
        )
        self.reset = (
            "08002000 <Reset_Handler>:\n 8002000: f000 b87e b.w 8002100 <_crt0_entry>\n"
        )
        if not runtime:
            self.symbols.update(
                gmk_trial_early_boot=(0x08002600, "T"),
                gmk_trial_platform_handoff=(0x08002700, "T"),
            )
            self.reset = (
                "08002000 <Reset_Handler>:\n 8002000: b672 cpsid i\n"
                " 8002002: f380 8808 msr MSP, r0\n"
                " 8002006: f380 8814 msr CONTROL, r0\n"
                " 800200a: f000 fa00 bl 8002600 <gmk_trial_early_boot>\n"
                " 800200e: f000 b87e b.w 8002100 <_crt0_entry>\n"
            )
        self.crt0 = (
            "08002100 <_crt0_entry>:\n 8002100: b672 cpsid i\n"
            " 8002102: f380 8808 msr MSP, r0\n"
            " 8002106: f380 8809 msr PSP, r0\n"
            " 800210a: f380 8814 msr CONTROL, r0\n"
            " 800210e: f000 f800 bl 8002900 <__early_init>\n"
        )
        self.handoff = (
            "08002700 <gmk_trial_platform_handoff>:\n 8002700: b672 cpsid i\n"
        )
        self.all_code = (
            " 8002400: f000 f900 bl 8002a00 <__wrap_transport_master_if_connected>\n"
            " 8002404: f000 f901 bl 8002b00 <__wrap_transaction_rpc_exec>\n"
        )
        self.vectors = (0x20001000, 0x08002001)

    def check(
        self, runtime: bool = True, optimized: bool = False
    ) -> subprocess.CompletedProcess[str]:
        (self.root / "symbols").write_text(
            "\n".join(
                f"{address:08x} 00000004 {kind} {name}"
                for name, (address, kind) in self.symbols.items()
            )
        )
        for name, contents in (
            ("sections", self.sections),
            ("Reset_Handler", self.reset),
            ("_crt0_entry", self.crt0),
            ("gmk_trial_platform_handoff", self.handoff),
            ("all", self.all_code),
        ):
            (self.root / name).write_text(contents)
        (self.root / "vectors").write_bytes(struct.pack("<II", *self.vectors))
        return subprocess.run(
            [
                sys.executable,
                *(["-O"] if optimized else []),
                str(CHECKER),
                *(["--runtime-watchdog"] if runtime else []),
                str(self.root / "trial.elf"),
            ],
            env={**os.environ, "PATH": f"{self.binaries}:{os.environ['PATH']}"},
            capture_output=True,
            text=True,
            check=False,
        )

    def test_runtime_accepts_upstream_weak_or_strong_reset_branch(self) -> None:
        for kind in ("W", "T"):
            with self.subTest(kind=kind):
                self.symbols["Reset_Handler"] = (0x08002000, kind)
                result = self.check()
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("runtime", result.stdout)

    def test_runtime_accepts_reset_alias_at_crt0_entry(self) -> None:
        self.symbols["Reset_Handler"] = self.symbols["_crt0_entry"]
        self.reset = ""
        self.vectors = (0x20001000, 0x08002101)
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_runtime_excludes_all_pre_crt_gpio_and_handoff_symbols(self) -> None:
        for forbidden in (
            "gmk_trial_early_boot",
            "gmk_trial_platform_handoff",
            "gmk_trial_role_read",
        ):
            with self.subTest(symbol=forbidden):
                self.symbols[forbidden] = (0x08003000, "T")
                result = self.check()
                del self.symbols[forbidden]
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(forbidden, result.stderr)

    def test_runtime_rejects_reset_that_executes_before_upstream_crt(self) -> None:
        self.reset = self.reset.replace(
            " 8002000:", " 8001ffe: b672 cpsid i\n 8002000:"
        )
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Reset_Handler", result.stderr)

    def test_runtime_requires_chibios_crt_initialization(self) -> None:
        self.crt0 = "08002100 <_crt0_entry>:\n 8002100: 4770 bx lr\n"
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ChibiOS", result.stderr)

    def test_runtime_requires_transport_wrapper_calls_and_trial_receiver(self) -> None:
        self.all_code = ""
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("transport_master_if_connected", result.stderr)
        self.fixture(runtime=True)
        del self.symbols["gmk_trial_process"]
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("gmk_trial_process", result.stderr)

    def test_runtime_preserves_retained_ram_and_vector_contract_under_optimization(
        self,
    ) -> None:
        self.sections = self.sections.replace("000040 00 WA", "000044 00 WA")
        result = self.check(optimized=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("retained", result.stderr)
        self.fixture(runtime=True)
        self.vectors = (0x20001000, 0x08002101)
        result = self.check(optimized=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("vector", result.stderr)

    def test_full_profile_keeps_strong_pre_crt_checks_under_optimization(self) -> None:
        self.fixture(runtime=False)
        result = self.check(runtime=False, optimized=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.symbols["Reset_Handler"] = (0x08002000, "W")
        result = self.check(runtime=False, optimized=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("strong", result.stderr)
        self.fixture(runtime=False)
        self.all_code = ""
        result = self.check(runtime=False, optimized=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("transport_master_if_connected", result.stderr)

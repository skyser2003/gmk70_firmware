"""Link actual allocation fixtures against the WB32 generated RAM contract."""

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "firmware/zmk/tools"))
import check_image

ROOT = Path(__file__).resolve().parents[2]
CMAKE = shutil.which("cmake") or str(ROOT / "firmware/zmk/tools/.venv/bin/cmake")
ADAPTER = ROOT / "firmware/zmk/soc/westberry/wb32fq95/linker.cmake"
TEMPLATE = """#define CONFIG_SRAM_SIZE 27
#define DT_CHOSEN(x) x
#define DT_REG_SIZE(x) 0x6f80
#define RAM_SIZE (CONFIG_SRAM_SIZE * 1K)
MEMORY { RAM (rw) : ORIGIN = 0x20000040, LENGTH = RAM_SIZE }
SECTIONS {
  .noinit (NOLOAD) : {
    _image_ram_start = .;
    *(.noinit)
    _image_ram_end = .;
  } > RAM
}
__kernel_ram_end = ORIGIN(RAM) + RAM_SIZE;
"""


class RamSymbolTests(unittest.TestCase):
    def symbols(self) -> dict[str, int]:
        return {
            "__gmk_ram_start": 0x20000040,
            "__gmk_ram_end": 0x20006FC0,
            "__kernel_ram_end": 0x20006FC0,
            "_image_ram_start": 0x20000040,
            "_image_ram_end": 0x20006BF8,
        }

    def test_full_extent_exposes_896_additional_bytes(self) -> None:
        result = check_image.validate_ram_symbols(self.symbols())
        self.assertEqual(result["ram_capacity"], 28544)
        self.assertEqual(result["ram_used"], 27576)
        self.assertEqual(result["ram_unallocated"], 968)
        self.assertEqual(result["ram_capacity"] - 27 * 1024, 896)

    def test_rounded_or_reserved_boundaries_and_missing_symbols_fail(self) -> None:
        for name, value in (
            ("__gmk_ram_start", 0x20000000),
            ("__gmk_ram_end", 0x20007040),
            ("__kernel_ram_end", 0x20006C40),
            ("_image_ram_start", 0x20000000),
            ("_image_ram_end", 0x20006FC1),
        ):
            with self.subTest(name=name), self.assertRaises(ValueError):
                symbols = self.symbols()
                symbols[name] = value
                check_image.validate_ram_symbols(symbols)
        for name in self.symbols():
            with self.subTest(missing=name), self.assertRaises(ValueError):
                symbols = self.symbols()
                del symbols[name]
                check_image.validate_ram_symbols(symbols)


@unittest.skipUnless(Path(CMAKE).is_file(), "CMake requires make setup-zmk")
class RamLinkerTests(unittest.TestCase):
    def generate(
        self, root: Path, template: str = TEMPLATE, options: str = ""
    ) -> tuple[subprocess.CompletedProcess[str], Path]:
        source, output = root / "upstream.ld", root / "wb32.ld"
        source.write_text(template)
        script = root / "configure.cmake"
        script.write_text(
            f'include("{ADAPTER.as_posix()}")\n'
            f"{options}\n"
            f'gmk70_configure_linker("{source.as_posix()}" "{output.as_posix()}")\n'
        )
        result = subprocess.run(
            [CMAKE, "-P", str(script)], capture_output=True, text=True
        )
        return result, output

    def test_template_drift_and_automatic_heap_are_rejected(self) -> None:
        for template, options in (
            (TEMPLATE.replace("#define RAM_SIZE (CONFIG_SRAM_SIZE * 1K)", ""), ""),
            (TEMPLATE + "#define RAM_SIZE (CONFIG_SRAM_SIZE * 1K)\n", ""),
            (TEMPLATE, "set(CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE -1)"),
            (TEMPLATE, "set(CONFIG_NEWLIB_LIBC ON)"),
        ):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as tmp:
                result, output = self.generate(Path(tmp), template, options)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    @unittest.skipUnless(shutil.which("cc") and shutil.which("ld"), "native linker")
    def test_recovered_space_links_and_retained_region_overflow_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            configured, generated = self.generate(root)
            self.assertEqual(configured.returncode, 0, configured.stderr)
            for name, length in (("old", "(27 * 1K)"), ("new", "0x6f80")):
                # The normal Zephyr build performs this same preprocessing.
                script = root / f"{name}.ld"
                if name == "old":
                    source = root / "original.ld"
                    source.write_text(TEMPLATE)
                else:
                    source = generated
                with script.open("w") as stream:
                    subprocess.run(
                        ["cc", "-E", "-P", "-x", "assembler-with-cpp", str(source)],
                        stdout=stream,
                        stderr=subprocess.PIPE,
                        text=True,
                        check=True,
                    )
                self.assertIn(f"LENGTH = {length}", script.read_text())
                for size in (27576, 27576 + 896, 28544, 28545):
                    obj, image = root / "allocation.o", root / "allocation.elf"
                    subprocess.run(
                        ["cc", "-x", "assembler", "-c", "-o", str(obj), "-"],
                        input=f'.section .noinit,"aw",@nobits\n.space {size}\n',
                        capture_output=True,
                        text=True,
                        check=True,
                    )
                    result = subprocess.run(
                        ["ld", "-T", str(script), "-o", str(image), str(obj)],
                        capture_output=True,
                        text=True,
                    )
                    with self.subTest(linker=name, size=size):
                        self.assertEqual(
                            result.returncode == 0,
                            size <= (28544 if name == "new" else 27648),
                        )

    @unittest.skipUnless(shutil.which("cc") and shutil.which("ld"), "native linker")
    def test_shifted_region_cannot_consume_retained_records(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            result, generated = self.generate(
                root, TEMPLATE.replace("0x20000040", "0x20000000")
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            script, obj = root / "link.ld", root / "allocation.o"
            with script.open("w") as stream:
                subprocess.run(
                    ["cc", "-E", "-P", "-x", "assembler-with-cpp", str(generated)],
                    stdout=stream,
                    check=True,
                )
            subprocess.run(
                ["cc", "-x", "assembler", "-c", "-o", str(obj), "-"],
                input='.section .noinit,"aw",@nobits\n.space 16\n',
                text=True,
                check=True,
            )
            linked = subprocess.run(
                ["ld", "-T", str(script), "-o", str(root / "bad.elf"), str(obj)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(linked.returncode, 0)
            self.assertIn("retained RAM", linked.stderr)


if __name__ == "__main__":
    unittest.main()

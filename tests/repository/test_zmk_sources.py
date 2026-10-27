import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import ModuleType

PROJECT = Path(__file__).resolve().parents[2]


def workspace_module() -> ModuleType:
    path = PROJECT / "firmware/zmk/tools/workspace.py"
    spec = importlib.util.spec_from_file_location("zmk_workspace", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ZmkBoundaryTests(unittest.TestCase):
    def test_frozen_manifest_has_exact_zmk_zephyr_and_cmsis(self) -> None:
        pins = json.loads((PROJECT / "dependencies/sources.lock.json").read_text())
        self.assertEqual(
            pins.get("zmk", {}).get("commit"),
            "4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85",
        )
        self.assertEqual(
            pins.get("zephyr", {}).get("commit"),
            "10ba6d0cb38bc3d258775d27982f707599320085",
        )
        self.assertEqual(
            pins.get("zmk_modules", {}).get("cmsis", {}).get("commit"),
            "d1b8b20b6278615b00e136374540eb1c00dcabe7",
        )

    def test_missing_dependency_requires_explicit_setup(self) -> None:
        module = workspace_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "dependencies").mkdir()
            (root / "dependencies/sources.lock.json").write_text(
                (PROJECT / "dependencies/sources.lock.json").read_text()
            )
            with self.assertRaisesRegex(ValueError, "setup-zmk"):
                module.verify(root)

    def test_dirty_and_wrong_commit_are_rejected_without_network(self) -> None:
        module = workspace_module()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            dependency = root / "dependencies/zmk"
            dependency.mkdir(parents=True)
            subprocess.run(["git", "init", "-q", str(dependency)], check=True)
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(dependency),
                    "-c",
                    "user.name=Test",
                    "-c",
                    "user.email=test@example.invalid",
                    "-c",
                    "commit.gpgsign=false",
                    "commit",
                    "--allow-empty",
                    "-qm",
                    "fixture",
                ],
                check=True,
            )
            commit = subprocess.check_output(
                ["git", "-C", str(dependency), "rev-parse", "HEAD"], text=True
            ).strip()
            lock = {
                "zmk": {
                    "repository": "https://example.invalid/zmk.git",
                    "commit": commit,
                },
                "zephyr": {"repository": "unused", "commit": "0" * 40},
                "zmk_modules": {},
            }
            (root / "dependencies/sources.lock.json").write_text(json.dumps(lock))
            (dependency / "changed.c").write_text("dirty")
            with self.assertRaisesRegex(ValueError, "local changes"):
                module.verify(root)
            (dependency / "changed.c").unlink()
            lock["zmk"]["commit"] = "0" * 40
            (root / "dependencies/sources.lock.json").write_text(json.dumps(lock))
            with self.assertRaisesRegex(ValueError, "does not match"):
                module.verify(root)


class ZmkImageTests(unittest.TestCase):
    def checker(self) -> ModuleType:
        path = PROJECT / "firmware/zmk/tools/check_image.py"
        spec = importlib.util.spec_from_file_location("zmk_check_image", path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_vectors_admit_staging_entry_and_reserved_ram(self) -> None:
        import struct

        result = self.checker().validate_vectors(
            struct.pack("<II", 0x20006C40, 0x08020001)
        )
        self.assertEqual(result["reset"], "0x08020001")

    def test_vectors_reject_retained_ram_or_resident_entry(self) -> None:
        import struct

        checker = self.checker()
        for stack, reset in [
            (0x20007000, 0x08020401),
            (0x20006FC0, 0x08002001),
            (0x20006FBF, 0x08020401),
        ]:
            with self.subTest(stack=stack, reset=reset), self.assertRaises(ValueError):
                checker.validate_vectors(struct.pack("<II", stack, reset))

    def test_oversized_image_cannot_reach_commit_metadata(self) -> None:
        import struct

        with self.assertRaisesRegex(ValueError, "120 KiB"):
            self.checker().validate_vectors(
                struct.pack("<II", 0x20006C40, 0x08020401) + bytes(120 * 1024)
            )

    def test_right_requires_actual_main_and_wrapped_startup(self) -> None:
        checker = self.checker()
        valid = "00000000 <bg_thread_main>:\n  0: bl 10 <__wrap_main>\n00000010 <__wrap_main>:\n  10: bl 20 <main>\n00000020 <main>:\n  20: bx lr\n"
        checker.validate_main_wrapping(valid)
        for broken in (
            valid.replace("<__wrap_main>\n", "<main>\n"),
            valid.replace("bl 20 <main>", "bl 20 <fake_main>"),
        ):
            with self.assertRaises(ValueError):
                checker.validate_main_wrapping(broken)

    def test_right_matrix_matches_maintained_board_pins(self) -> None:
        import re

        board = json.loads(
            (
                PROJECT / "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json"
            ).read_text()
        )
        dts = (
            PROJECT / "firmware/zmk/boards/zuoya/gmk70_right/gmk70_right.dts"
        ).read_text()
        for kind in ("row", "col"):
            declaration = dts.split(f"{kind}-gpios =", 1)[1].split(";", 1)[0]
            actual = [
                bank.upper() + pin
                for bank, pin in re.findall(r"<&gpio([abc]) (\d+)", declaration)
            ]
            self.assertEqual(actual, board["split"]["matrix_pins"]["right"][kind + "s"])
        self.assertIn("columns = <9>", dts)

    def test_build_routes_explicit_right_without_changing_left_default(self) -> None:
        build = (PROJECT / "scripts/build_zmk.sh").read_text()
        self.assertIn("${1:-left-trial}", build)
        self.assertIn('"$variant" == right-trial', build)
        self.assertIn("artifacts/zmk/$variant", build)
        self.assertIn('--board "$board"', build)


class ZmkElfTests(unittest.TestCase):
    def checker(self) -> ModuleType:
        return ZmkImageTests().checker()

    def elf(self, address: int, load: int, filesz: int, memsz: int) -> bytes:
        import struct

        data = bytearray(256)
        data[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<H", data, 18, 40)
        struct.pack_into("<II", data, 24, 0x08020001, 52)
        struct.pack_into("<HH", data, 42, 32, 1)
        struct.pack_into("<8I", data, 52, 1, 128, address, load, filesz, memsz, 6, 8)
        return bytes(data)

    def test_elf_preserves_both_retained_ram_ranges(self) -> None:
        checker = self.checker()
        checker.validate_elf(self.elf(0x20000040, 0x08021000, 16, 1024), 0x08020001)
        for address, size in [(0x20000000, 64), (0x20006F80, 128)]:
            with (
                self.subTest(address=address),
                self.assertRaisesRegex(ValueError, "protected memory"),
            ):
                checker.validate_elf(self.elf(address, 0x08021000, 0, size), 0x08020001)

    def test_elf_rejects_initialized_load_outside_staging(self) -> None:
        with self.assertRaisesRegex(ValueError, "outside staging"):
            self.checker().validate_elf(
                self.elf(0x20000040, 0x08002000, 16, 16), 0x08020001
            )


if __name__ == "__main__":
    unittest.main()

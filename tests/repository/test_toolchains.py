"""Exercise explicit compiler selection and archive verification during setup."""

import hashlib
import io
import json
import os
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
COMPILERS = {
    "arm": ("arm_toolchain", "arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi", "xz"),
    "xpack": ("xpack_arm_toolchain", "xpack-arm-none-eabi-gcc-14.2.1-1.1", "gz"),
}


class ToolchainSetupTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in ("setup.sh", "project.sh"):
            self.write(f"scripts/{name}", (PROJECT / "scripts" / name).read_text())
        # Source verification and uv syncing are independent of the compiler
        # archive. Keep this fixture offline and observe their calls only.
        self.write("dependencies/source-files.json", "{}")
        self.write("dependencies/qmk/quantum/quantum.c", "fixture")
        self.write("tools/build-support/project_build.py", "raise SystemExit(0)\n")
        self.write(
            ".work/toolchains/uv-x86_64-unknown-linux-gnu/uv",
            '#!/bin/sh\nprintf "%s\\n" "$*" >> .work/uv-calls\n',
            executable=True,
        )
        self.pins: dict[str, dict[str, str]] = {}
        for selection, (key, directory, compression) in COMPILERS.items():
            archive = self.root / f"{selection}.tar.{compression}"
            payload = f"#!/bin/sh\necho {selection}-fixture\n".encode()
            with tarfile.open(
                archive, "w:xz" if compression == "xz" else "w:gz"
            ) as bundle:
                entry = tarfile.TarInfo(f"{directory}/bin/arm-none-eabi-gcc")
                entry.size = len(payload)
                entry.mode = 0o755
                bundle.addfile(entry, io.BytesIO(payload))
            self.pins[key] = {
                "url": f"https://example.invalid/{archive.name}",
                "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            }
        self.write("dependencies/sources.lock.json", json.dumps(self.pins))
        self.write(
            "bin/curl",
            "#!/usr/bin/env python3\n"
            "import pathlib, shutil, sys\n"
            "url = next(arg for arg in sys.argv if arg.startswith('https://'))\n"
            "with open('.work/download-urls', 'a') as output:\n"
            "    output.write(url + '\\n')\n"
            "shutil.copyfile(pathlib.Path(url.rsplit('/', 1)[1]), "
            "sys.argv[sys.argv.index('-o') + 1])\n",
            executable=True,
        )
        self.environment = {
            **os.environ,
            "PATH": f"{self.root}/bin:{os.environ['PATH']}",
        }
        self.environment.pop("GMK70_TOOLCHAIN", None)

    def write(self, name: str, content: str, *, executable: bool = False) -> None:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        if executable:
            path.chmod(0o755)

    def setup(self, selection: str | None = None) -> subprocess.CompletedProcess[str]:
        environment = dict(self.environment)
        if selection is not None:
            environment["GMK70_TOOLCHAIN"] = selection
        return subprocess.run(
            ["bash", "scripts/setup.sh"],
            cwd=self.root,
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_default_arm_and_explicit_xpack_use_their_own_archive_and_binary(
        self,
    ) -> None:
        for selection in ("arm", "xpack"):
            with self.subTest(selection=selection):
                result = self.setup(None if selection == "arm" else selection)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f"{selection}-fixture", result.stdout)
                result = subprocess.run(
                    [
                        "bash",
                        "-c",
                        "source scripts/project.sh; arm-none-eabi-gcc --version",
                    ],
                    cwd=self.root,
                    env={**self.environment, "GMK70_TOOLCHAIN": selection},
                    capture_output=True,
                    text=True,
                    check=True,
                )
                self.assertEqual(result.stdout.strip(), f"{selection}-fixture")
        self.assertEqual(
            (self.root / ".work/download-urls").read_text().splitlines(),
            [self.pins[COMPILERS[name][0]]["url"] for name in ("arm", "xpack")],
        )

    def test_checksum_failure_cannot_install_or_silently_switch_compilers(self) -> None:
        self.pins["xpack_arm_toolchain"]["sha256"] = "0" * 64
        self.write("dependencies/sources.lock.json", json.dumps(self.pins))
        result = self.setup("xpack")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FAILED", result.stdout)
        self.assertEqual(
            (self.root / ".work/download-urls").read_text().splitlines(),
            [self.pins["xpack_arm_toolchain"]["url"]],
        )
        for _, directory, _ in COMPILERS.values():
            self.assertFalse((self.root / ".work/toolchains" / directory).exists())
        self.assertFalse((self.root / ".work/uv-calls").exists())


if __name__ == "__main__":
    unittest.main()

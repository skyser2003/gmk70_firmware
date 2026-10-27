"""Exercise Vial distribution boundaries with a local compiler substitute."""

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
import zipfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]


class VialBuildTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        scripts = self.root / "scripts"
        scripts.mkdir()
        for name in ("build.sh", "build_vial.sh", "project.sh"):
            shutil.copyfile(PROJECT / "scripts" / name, scripts / name)
        for name, contents in {
            "dependencies/sources.lock.json": "{}",
            "firmware/qmk-vial/keymaps/vial/vial.json": "{}",
            "tools/firmware-update/update.py": "# updater tool\n",
            "tools/firmware-update/trial.py": "# offline trial tool\n",
            "tools/firmware-update/read_radio.py": "# read-only radio tool\n",
            "docs/ZMK-TRIAL-LAUNCHER.md": "Trial receiver experiment\n",
            "docs/RADIO-DIAGNOSTICS.md": "Radio experiment\n",
        }.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
        self.binaries = self.root / "bin"
        self.binaries.mkdir()
        self.tool(
            "source-python",
            """
            import os, pathlib, sys
            if sys.argv[1] == '-m':
                os.execv(sys.executable, [sys.executable, *sys.argv[1:]])
            if sys.argv[1].endswith('project_build.py'):
                if sys.argv[2] == 'prepare-vial':
                    mode = sys.argv[sys.argv.index('--mode') + 1]
                    path = pathlib.Path('.work/vial/vial') / mode
                    path.mkdir(parents=True, exist_ok=True)
                    (path / 'gmk70-source-origins.json').write_text('{}')
                    print(path.resolve())
                elif sys.argv[2] == 'export-vial':
                    pathlib.Path(sys.argv[3]).write_text('working sources')
                else:
                    sys.exit(7)
            elif sys.argv[1].endswith('update_package.py'):
                output = pathlib.Path(sys.argv[3])
                for name in ('application.bin', 'application.hex', 'resident.bin',
                             'resident.elf', 'resident.map', 'install.bin',
                             'install.hex', 'update.json'):
                    (output / name).write_text(name)
            else:
                sys.exit(8)
            """,
        )
        self.tool(
            "make",
            """
            import os, pathlib, sys
            arguments = dict(arg.split('=', 1) for arg in sys.argv[1:] if '=' in arg)
            build = pathlib.Path(arguments['BUILD_DIR'])
            build.mkdir(parents=True, exist_ok=True)
            target = 'zuoya_gmk70_vial'
            for extension in ('bin', 'hex', 'elf', 'map'):
                (build / f'{target}.{extension}').write_text('compiled image')
            obj = build / f'obj_{target}'
            obj.mkdir(exist_ok=True)
            features = '''VIAL_ENABLE VIALRGB_ENABLE QMK_SETTINGS TAP_DANCE_ENABLE
                COMBO_ENABLE KEY_OVERRIDE_ENABLE CAPS_WORD_ENABLE LAYER_LOCK_ENABLE
                REPEAT_KEY_ENABLE AUTO_SHIFT_ENABLE MOUSEKEY_ENABLE TAPPING_TERM_PER_KEY
                QUICK_TAP_TERM_PER_KEY CHORDAL_HOLD FLOW_TAP_TERM'''.split()
            for variable in ('GMK70_SPLIT_UPDATE', 'GMK70_RADIO_DIAGNOSTICS',
                             'GMK70_VIAL_TRIAL'):
                if arguments.get(variable) == 'yes':
                    features.append(variable)
            if arguments.get('GMK70_SPLIT_UPDATE') == 'yes':
                features.append('GMK70_VIAL_UPDATE')
            insecure = arguments.get('VIAL_INSECURE') == 'yes'
            if os.environ.get('FAKE_VIAL_INSECURE') in ('yes', 'no'):
                insecure = os.environ['FAKE_VIAL_INSECURE'] == 'yes'
            if insecure:
                features.append('VIAL_INSECURE')
            (obj / 'cflags.txt').write_text(' '.join('-D' + flag for flag in features))
            (obj / 'ldflags.txt').write_text('link flags')
            """,
        )
        self.tool(
            "arm-none-eabi-objcopy",
            "import pathlib, sys; pathlib.Path(sys.argv[-1]).write_text('compiled image')",
        )
        self.tool("arm-none-eabi-nm", "print('00000000 T gmk_trial_process')")
        for name in ("qmk", "arm-none-eabi-gcc", "arm-none-eabi-size"):
            self.tool(name, "print('local tool fixture')")

    def tool(self, name: str, body: str) -> None:
        path = self.binaries / name
        path.write_text("#!/usr/bin/env python3\n" + textwrap.dedent(body))
        path.chmod(0o755)

    def build(self, mode: str, insecure: str = "") -> subprocess.CompletedProcess[str]:
        environment = dict(os.environ)
        environment.update(
            PATH=f"{self.binaries}:{environment['PATH']}",
            GMK70_SOURCE_PYTHON=str(self.binaries / "source-python"),
            FAKE_VIAL_INSECURE=insecure,
        )
        return subprocess.run(
            ["bash", "scripts/build.sh", "vial", "vial", mode],
            cwd=self.root,
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_explicit_unlocked_modes_compile_and_package_as_permanently_unlocked(
        self,
    ) -> None:
        for mode in ("unlocked-updater", "unlocked-trial-updater"):
            with self.subTest(mode=mode):
                result = self.build(mode)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                output = self.root / "artifacts/vial" / f"vial-{mode}"
                self.assertIn("-DVIAL_INSECURE", (output / "cflags.txt").read_text())
                for name in ("README.txt", "build-info.txt"):
                    self.assertIn("PERMANENTLY UNLOCKED", (output / name).read_text())
                with zipfile.ZipFile(output / f"gmk70-vial-{mode}.zip") as package:
                    self.assertIn("application.bin", package.namelist())
                    self.assertIn("update.json", package.namelist())
                    self.assertIn("FIRMWARE-SHA256SUMS", package.namelist())
                    if mode == "unlocked-trial-updater":
                        self.assertIn("trial.py", package.namelist())
                        self.assertIn("TRIAL-LAUNCHER.md", package.namelist())
                        self.assertIn(
                            "-DGMK70_VIAL_TRIAL", (output / "cflags.txt").read_text()
                        )
                    else:
                        self.assertNotIn("trial.py", package.namelist())
                        self.assertNotIn(
                            "-DGMK70_VIAL_TRIAL", (output / "cflags.txt").read_text()
                        )
                for line in (output / "SHA256SUMS").read_text().splitlines():
                    digest, name = line.split("  ", 1)
                    self.assertEqual(
                        hashlib.sha256((output / name).read_bytes()).hexdigest(), digest
                    )
                self.assertEqual(json.loads((output / "vial.json").read_text()), {})

    def test_unlocked_modes_require_the_compiled_insecure_flag(self) -> None:
        for mode in ("unlocked-updater", "unlocked-trial-updater"):
            with self.subTest(mode=mode):
                result = self.build(mode, insecure="no")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("VIAL_INSECURE", result.stdout + result.stderr)

    def test_secure_modes_reject_compiled_insecure_flag(self) -> None:
        for mode in ("normal", "updater", "trial-updater", "radio-diagnostic-updater"):
            with self.subTest(mode=mode):
                result = self.build(mode, insecure="yes")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "physical unlock was disabled", result.stdout + result.stderr
                )

    def test_secure_updater_still_instructs_physical_unlock(self) -> None:
        result = self.build("updater")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = self.root / "artifacts/vial/vial-updater"
        self.assertNotIn("-DVIAL_INSECURE", (output / "cflags.txt").read_text())
        self.assertIn(
            "first unlock in the Vial GUI", (output / "README.txt").read_text()
        )

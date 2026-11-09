"""Check container boundaries without requiring a Docker daemon in native CI."""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]


class DockerCommandTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "checkout"
        (self.root / "scripts").mkdir(parents=True)
        for name in ("docker.sh", "project.sh"):
            shutil.copyfile(PROJECT / "scripts" / name, self.root / "scripts" / name)
        self.bin = Path(self.temporary.name) / "bin"
        self.bin.mkdir()
        self.log = Path(self.temporary.name) / "calls.jsonl"
        docker = self.bin / "docker"
        docker.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, sys\n"
            "with open(os.environ['DOCKER_TEST_LOG'], 'a') as output:\n"
            "    output.write(json.dumps(sys.argv[1:]) + '\\n')\n"
            "if sys.argv[1:3] == ['image', 'inspect']:\n"
            "    sys.exit(int(os.environ.get('DOCKER_TEST_MISSING', '0')))\n"
        )
        docker.chmod(0o755)
        self.environment = {
            key: value
            for key, value in os.environ.items()
            if key
            not in ("CODEX_PROXY_CERT", "GMK70_DOCKER_CA_BUNDLE", "GMK70_TOOLCHAIN")
        }
        self.environment.update(
            PATH=f"{self.bin}:{os.environ['PATH']}",
            DOCKER_TEST_LOG=str(self.log),
            GMK70_DOCKER_IMAGE="gmk70-test:local",
        )

    def run_wrapper(self, *args: str, success: bool = True) -> list[list[str]]:
        self.log.unlink(missing_ok=True)
        result = subprocess.run(
            ["bash", "scripts/docker.sh", *args],
            cwd=self.root,
            env=self.environment,
            capture_output=True,
            text=True,
            check=False,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        if not self.log.exists():
            return []
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_build_is_offline_and_preserves_arguments_and_native_cache(self) -> None:
        # Offline builds do not depend on the setup session's CA still existing.
        self.environment["GMK70_DOCKER_CA_BUNDLE"] = "/missing/session-ca.pem"
        native = self.root / "tools/build-support/.venv"
        native.mkdir(parents=True)
        (native / "marker").write_text("host environment")
        args = self.run_wrapper("run", "make", "build", "VARIABLE=two words")[-1]
        self.assertEqual(args[args.index("--network") + 1], "none")
        self.assertIn("UV_OFFLINE=1", args)
        self.assertEqual(args[args.index("--user") + 1], f"{os.getuid()}:{os.getgid()}")
        self.assertEqual(
            args[-4:], ["gmk70-test:local", "make", "build", "VARIABLE=two words"]
        )
        self.assertIn(
            f"type=bind,src={self.root}/.work/docker/xpack/work,dst={self.root}/.work",
            args,
        )
        self.assertIn("GMK70_TOOLCHAIN=xpack", args)
        self.assertEqual((native / "marker").read_text(), "host environment")
        self.assertFalse(
            any("--privileged" in item or "/dev/" in item for item in args)
        )

    def test_setup_is_explicit_and_mounts_trust_read_only(self) -> None:
        ca = Path(self.temporary.name) / "CA bundle.pem"
        ca.write_text("test CA")
        self.environment["GMK70_DOCKER_CA_BUNDLE"] = str(ca)
        args = self.run_wrapper("setup")[-1]
        self.assertIn("GMK70_TOOLCHAIN=xpack", args)
        self.assertNotIn("--network", args)
        self.assertEqual(args[-4:], ["make", "setup", "setup-vial", "setup-zmk"])
        self.assertIn(f"type=bind,src={ca},dst=/run/gmk70-ca.pem,readonly", args)
        self.assertIn("GIT_SSL_CAINFO=/run/gmk70-ca.pem", args)
        self.assertEqual(
            self.run_wrapper("setup", "setup-zmk")[-1][-2:], ["make", "setup-zmk"]
        )
        self.assertEqual(self.run_wrapper("setup", "build", success=False), [])

    def test_arm_selection_uses_a_separate_cache_and_reaches_the_container(
        self,
    ) -> None:
        self.environment["GMK70_TOOLCHAIN"] = "arm"
        for operation in (("setup",), ("run", "make", "build")):
            args = self.run_wrapper(*operation)[-1]
            self.assertIn("GMK70_TOOLCHAIN=arm", args)
            self.assertIn(
                f"type=bind,src={self.root}/.work/docker/arm/work,dst={self.root}/.work",
                args,
            )
            self.assertNotIn(
                f"type=bind,src={self.root}/.work/docker/xpack/work,dst={self.root}/.work",
                args,
            )
        self.environment["GMK70_TOOLCHAIN"] = "unknown"
        self.assertEqual(self.run_wrapper("setup", success=False), [])

    def test_project_environment_can_override_the_docker_default(self) -> None:
        (self.root / ".env").write_text("GMK70_TOOLCHAIN=arm\n")
        args = self.run_wrapper("setup")[-1]
        self.assertIn("GMK70_TOOLCHAIN=arm", args)
        self.assertIn(
            f"type=bind,src={self.root}/.work/docker/arm/work,dst={self.root}/.work",
            args,
        )

    def test_image_context_excludes_checkout_and_uses_secret(self) -> None:
        ca = Path(self.temporary.name) / "ca.pem"
        ca.write_text("test CA")
        self.environment["GMK70_DOCKER_CA_BUNDLE"] = str(ca)
        args = self.run_wrapper("image")[-1]
        self.assertEqual(args[0], "build")
        self.assertEqual(args[-1], str(self.root / "docker"))
        self.assertIn(f"id=proxy_ca,src={ca}", args)
        self.assertNotIn("--build-arg", args)

    def test_missing_image_and_redirected_cache_fail_before_run(self) -> None:
        self.environment["DOCKER_TEST_MISSING"] = "1"
        self.assertEqual(
            len(self.run_wrapper("run", "make", "build", success=False)), 1
        )
        del self.environment["DOCKER_TEST_MISSING"]
        (self.root / ".work").symlink_to(
            Path(self.temporary.name), target_is_directory=True
        )
        self.assertEqual(
            len(self.run_wrapper("run", "make", "build", success=False)), 1
        )

    def test_linked_worktree_mounts_common_git_metadata(self) -> None:
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "add", "scripts"], check=True)
        subprocess.run(
            [
                "git",
                "-C",
                str(self.root),
                "-c",
                "user.name=Test",
                "-c",
                "user.email=test@example.invalid",
                "-c",
                "commit.gpgsign=false",
                "commit",
                "-qm",
                "fixture",
            ],
            check=True,
        )
        common = self.root / ".git"
        worktree = Path(self.temporary.name) / "linked"
        subprocess.run(
            [
                "git",
                "-C",
                str(self.root),
                "worktree",
                "add",
                "-q",
                "--detach",
                str(worktree),
            ],
            check=True,
        )
        self.root = worktree
        args = self.run_wrapper("run", "git", "status")[-1]
        self.assertIn(f"type=bind,src={common},dst={common}", args)


if __name__ == "__main__":
    unittest.main()

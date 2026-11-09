"""Exercise release-family routing and prevent mixed firmware releases."""

import hashlib
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[2]


class ReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        source = PROJECT / "tools/build-support/github_release.py"
        self.assertTrue(source.is_file(), "Missing independent release routing")
        spec = importlib.util.spec_from_file_location("github_release", source)
        assert spec is not None and spec.loader is not None
        self.release = importlib.util.module_from_spec(spec)
        with patch.object(sys, "path", [str(source.parent), *sys.path]):
            spec.loader.exec_module(self.release)
        self.notes_patch = patch.object(self.release, "write_diff_notes")
        self.notes_patch.start()
        self.addCleanup(self.notes_patch.stop)
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def test_release_branch_pushes_build_only_their_family(self) -> None:
        via = self.release.plan("push", "refs/heads/release/via")
        vial = self.release.plan("push", "refs/heads/release/vial")
        zmk = self.release.plan("push", "refs/heads/release/zmk")
        self.assertEqual(
            zmk,
            {"family": "zmk", "variants": ("zmk", "zmk-standalone"), "publish": True},
        )
        self.assertTrue(via["publish"])
        self.assertTrue(vial["publish"])
        self.assertEqual(via["family"], "via")
        self.assertEqual(vial["family"], "vial")
        self.assertEqual(
            set(via["variants"]),
            {"via-release", "via-debug", "via-release-updater", "via-debug-updater"},
        )
        self.assertEqual(
            set(vial["variants"]), {"vial", "vial-updater", "vial-unlocked-updater"}
        )
        self.assertTrue(set(via["variants"]).isdisjoint(vial["variants"]))

    def test_non_push_events_cannot_plan_checks_or_builds(self) -> None:
        for family in ("via", "vial", "zmk"):
            for event in (
                "pull_request",
                "pull_request_target",
                "workflow_dispatch",
                "schedule",
                "release",
                "workflow_run",
            ):
                with (
                    self.subTest(family=family, event=event),
                    self.assertRaisesRegex(ValueError, "Only pushes"),
                ):
                    self.release.plan(event, f"refs/heads/release/{family}")

    def test_main_tags_and_other_refs_cannot_plan_checks_or_builds(self) -> None:
        for ref in (
            "refs/heads/main",
            "refs/heads/codex/work",
            "refs/heads/release/other",
            "refs/heads/release/zmk/extra",
            "release/zmk",
            "refs/pull/42/merge",
            "refs/tags/via-v1.2.3",
            "refs/tags/vial-4.5.6",
            "refs/tags/zmk-v1.2.3",
            "refs/tags/zmk-v1.2.3-trial",
        ):
            with (
                self.subTest(ref=ref),
                self.assertRaisesRegex(ValueError, "Only pushes"),
            ):
                self.release.plan("push", ref)

    def test_cli_emits_no_job_plan_for_an_unsupported_context(self) -> None:
        for event, ref in (
            ("push", "refs/heads/main"),
            ("push", "refs/tags/zmk-v0.1.0"),
            ("workflow_dispatch", "refs/heads/release/zmk"),
        ):
            result = subprocess.run(
                [
                    sys.executable,
                    str(PROJECT / "tools/build-support/github_release.py"),
                    "plan",
                ],
                env=dict(os.environ, GITHUB_EVENT_NAME=event, GITHUB_REF=ref),
                text=True,
                capture_output=True,
                check=False,
            )
            with self.subTest(event=event, ref=ref):
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertIn("Only pushes", result.stderr)

    def test_unmatched_publication_cannot_read_or_write_release_assets(self) -> None:
        for family, ref in (
            ("via", "refs/heads/main"),
            ("vial", "refs/heads/main"),
            ("zmk", "refs/heads/main"),
            ("via", "refs/tags/via-v1.2.3"),
            ("vial", "refs/tags/vial-1.2.3"),
            ("zmk", "refs/tags/zmk-v1.2.3"),
            ("via", "refs/heads/release/vial"),
            ("vial", "refs/heads/release/via"),
            ("zmk", "refs/heads/release/vial"),
            ("via", "refs/heads/release/zmk"),
        ):
            with (
                self.subTest(family=family, ref=ref),
                patch.object(self.release, "verify_assets") as verify,
                patch.object(self.release, "github_pages") as read,
                patch.object(self.release, "run_gh") as write,
            ):
                with self.assertRaises(ValueError):
                    self.release.publish(
                        family,
                        ref,
                        "a" * 40,
                        "owner/repo",
                        self.root / "not-built",
                        self.root / "notes.md",
                    )
                verify.assert_not_called()
                read.assert_not_called()
                write.assert_not_called()

    def test_workflow_triggers_only_on_release_branch_pushes(self) -> None:
        workflow = (PROJECT / ".github/workflows/release.yaml").read_text()
        triggers = workflow.split("\non:\n", 1)[1].split("\npermissions:", 1)[0]
        self.assertEqual(re.findall(r"^  ([a-z_]+):", triggers, re.MULTILINE), ["push"])
        self.assertEqual(
            re.findall(r"^    ([a-z_]+):", triggers, re.MULTILINE), ["branches"]
        )
        self.assertIn("branches: [release/via, release/vial, release/zmk]", triggers)
        self.assertIn("needs: plan", workflow)
        self.assertIn("needs: [plan, checks]", workflow)
        self.assertIn("needs: [plan, checks, build]", workflow)
        self.assertNotIn("inputs.family", workflow)
        self.assertNotIn("plan --family", workflow)

    def test_experimental_trial_and_diagnostic_builds_are_not_releases(self) -> None:
        vial = self.release.plan("push", "refs/heads/release/vial")
        self.assertIn("vial-unlocked-updater", vial["variants"])
        for experiment in (
            "vial-trial-updater",
            "vial-unlocked-trial-updater",
            "vial-radio-diagnostic-updater",
            "vial-trial-updater-runtime-watchdog",
            "vial-unlocked-trial-updater-runtime-watchdog",
        ):
            self.assertNotIn(experiment, vial["variants"])

    def test_trial_artifacts_block_normal_vial_publication(self) -> None:
        output = self.assets("vial")
        for name in (
            "gmk70-vial-unlocked-trial-updater.zip",
            "gmk70-vial-unlocked-trial-updater-runtime-watchdog.zip",
            "gmk70-vial-trial-updater-runtime-watchdog.zip",
        ):
            with self.subTest(asset=name):
                artifact = output / name
                artifact.write_text("experiment")
                with self.assertRaisesRegex(ValueError, "Unexpected release assets"):
                    self.release.verify_assets(output, "vial")
                self.assertFalse((output / "SHA256SUMS").exists())
                artifact.unlink()

    def test_trial_and_bridge_artifacts_block_zmk_publication(self) -> None:
        output = self.assets("zmk")
        for variant in (
            "wired-studio-update-test",
            "wired-studio-radio-qualification",
            "wired-studio-native-bridge",
            "left-trial",
            "right-trial",
        ):
            with self.subTest(variant=variant):
                artifact = output / f"gmk70-zmk-{variant}.zip"
                artifact.write_text("not a canonical release")
                with self.assertRaisesRegex(ValueError, "Unexpected release assets"):
                    self.release.verify_assets(output, "zmk")
                artifact.unlink()

    def test_patch_versions_are_independent_and_sorted_numerically(self) -> None:
        tags = [
            ("via-v1.2.9", "old"),
            ("via-v1.2.10", "other"),
            ("vial-9.9.9", "other"),
            ("zmk-v0.2.9", "other"),
            ("zmk-v0.2.10", "other"),
            ("via-v01.2.99", "other"),
        ]
        self.assertEqual(self.release.choose_tag("via", tags, "new"), "via-v1.2.11")
        self.assertEqual(self.release.choose_tag("vial", tags, "new"), "vial-9.9.10")
        self.assertEqual(self.release.choose_tag("via", [], "new"), "via-v0.1.0")
        self.assertEqual(self.release.choose_tag("vial", [], "new"), "vial-0.1.0")
        self.assertEqual(self.release.choose_tag("zmk", tags, "new"), "zmk-v0.2.11")
        self.assertEqual(self.release.choose_tag("zmk", [], "new"), "zmk-v0.1.0")

    def test_retry_reuses_tag_for_the_same_family_and_commit(self) -> None:
        tags = [("via-v0.1.0", "same"), ("vial-0.1.9", "same"), ("via-v0.1.3", "newer")]
        self.assertEqual(self.release.choose_tag("via", tags, "same"), "via-v0.1.0")

    def assets(self, family: str) -> Path:
        output = self.root / family
        output.mkdir()
        with patch.object(
            sys, "path", [str(PROJECT / "tools/build-support"), *sys.path]
        ):
            from firmware_assets import (
                UPDATE_FILES,
                image_name,
                is_updater,
                write_package,
            )
        for variant in self.release.FAMILIES[family]:
            source = self.root / f"source-{family}-{variant}"
            source.mkdir()
            names = UPDATE_FILES if is_updater(variant) else (image_name(variant),)
            for name in names:
                (source / name).write_text(f"firmware for {variant}: {name}")
            write_package(source, output, variant)
        return output

    def test_verified_release_contains_only_selected_family(self) -> None:
        for family in ("via", "vial", "zmk"):
            with self.subTest(family=family):
                output = self.assets(family)
                self.release.verify_assets(output, family)
                manifest = (output / "SHA256SUMS").read_text()
                self.assertEqual(
                    len(manifest.splitlines()), len(self.release.FAMILIES[family])
                )
                other = "vial" if family == "via" else "via-release"
                self.assertNotIn(f"gmk70-{other}.zip", manifest)

    def test_mixed_family_artifacts_block_publication(self) -> None:
        output = self.assets("via")
        (output / "gmk70-vial.zip").write_text("wrong firmware family")
        with self.assertRaisesRegex(ValueError, "Unexpected release assets"):
            self.release.verify_assets(output, "via")
        self.assertFalse((output / "SHA256SUMS").exists())

    def test_checksummed_hex_asset_blocks_publication(self) -> None:
        for family in ("via", "vial", "zmk"):
            with self.subTest(family=family):
                output = self.assets(family)
                variant = self.release.FAMILIES[family][0]
                artifact = output / f"gmk70-{variant}.hex"
                artifact.write_bytes(b"obsolete HEX firmware")
                with (output / f"gmk70-{variant}.sha256").open("a") as manifest:
                    manifest.write(
                        f"{hashlib.sha256(artifact.read_bytes()).hexdigest()}  {artifact.name}\n"
                    )
                with self.assertRaisesRegex(ValueError, "Unsupported release asset"):
                    self.release.verify_assets(output, family)
                self.assertFalse((output / "SHA256SUMS").exists())

    def test_corrupt_release_asset_blocks_publication(self) -> None:
        output = self.assets("vial")
        (output / "gmk70-vial.bin").write_text("corrupt")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            self.release.verify_assets(output, "vial")
        self.assertFalse((output / "SHA256SUMS").exists())

    def test_extra_zip_contents_block_publication_even_with_valid_outer_hash(
        self,
    ) -> None:
        output = self.assets("vial")
        archive = output / "gmk70-vial-updater.zip"
        with zipfile.ZipFile(archive, "a") as bundle:
            bundle.writestr("README.txt", "not a release firmware file")
        (output / "gmk70-vial-updater.sha256").write_text(
            f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n"
        )
        with self.assertRaisesRegex(ValueError, "only firmware"):
            self.release.verify_assets(output, "vial")
        self.assertFalse((output / "SHA256SUMS").exists())

    def test_upload_failure_never_publishes_the_draft(self) -> None:
        output = self.assets("vial")
        commands = []

        def command(arguments: list[str]) -> None:
            commands.append(arguments)
            if arguments[:2] == ["release", "upload"]:
                raise subprocess.CalledProcessError(1, ["gh", *arguments])

        with (
            patch.object(self.release, "github_pages", side_effect=[[], []]),
            patch.object(self.release, "run_gh", side_effect=command),
            self.assertRaises(subprocess.CalledProcessError),
        ):
            self.release.publish(
                "vial",
                "refs/heads/release/vial",
                "a" * 40,
                "owner/repo",
                output,
                self.root / "notes.md",
            )
        self.assertFalse(
            any(arguments[:2] == ["release", "edit"] for arguments in commands)
        )

    def test_successful_publication_tags_the_built_commit_for_each_family(self) -> None:
        for family, expected_tag in (
            ("via", "via-v0.1.0"),
            ("vial", "vial-0.1.0"),
            ("zmk", "zmk-v0.1.0"),
        ):
            with self.subTest(family=family):
                output = self.assets(family)
                with (
                    patch.object(self.release, "github_pages", side_effect=[[], []]),
                    patch.object(self.release, "run_gh") as write,
                ):
                    tag = self.release.publish(
                        family,
                        f"refs/heads/release/{family}",
                        "a" * 40,
                        "owner/repo",
                        output,
                        self.root / "notes.md",
                    )
                self.assertEqual(tag, expected_tag)
                commands = [call.args[0] for call in write.call_args_list]
                self.assertIn(f"ref=refs/tags/{expected_tag}", commands[0])
                self.assertIn(f"sha={'a' * 40}", commands[0])
                create = commands[1]
                self.assertEqual(create[create.index("--target") + 1], "a" * 40)
                self.assertIn("--draft", create)
                self.assertNotIn("--generate-notes", create)
                if family == "zmk":
                    self.assertEqual(
                        create[create.index("--title") + 1], "GMK70 ZMK zmk-v0.1.0"
                    )
                self.assertEqual(commands[2][:2], ["release", "upload"])
                self.assertEqual(commands[3][:2], ["release", "edit"])
                self.assertIn("--draft=false", commands[3])

    def test_published_release_is_immutable_on_retry(self) -> None:
        output = self.assets("via")
        tags = [{"name": "via-v0.1.0", "commit": {"sha": "a" * 40}}]
        releases = [
            {
                "tag_name": "via-v0.1.0",
                "draft": False,
                "html_url": "https://example.test/existing",
            }
        ]
        with (
            patch.object(self.release, "github_pages", side_effect=[tags, releases]),
            patch.object(self.release, "run_gh") as write,
        ):
            self.release.publish(
                "via",
                "refs/heads/release/via",
                "a" * 40,
                "owner/repo",
                output,
                self.root / "notes.md",
            )
        write.assert_not_called()

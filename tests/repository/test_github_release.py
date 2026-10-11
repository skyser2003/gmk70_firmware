"""Exercise release-family routing and prevent mixed firmware releases."""

import hashlib
import importlib.util
import subprocess
import tempfile
import unittest
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
        spec.loader.exec_module(self.release)
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def test_release_branch_pushes_build_only_their_family(self) -> None:
        via = self.release.plan("push", "refs/heads/release/via")
        vial = self.release.plan("push", "refs/heads/release/vial")
        self.assertTrue(via["publish"])
        self.assertTrue(vial["publish"])
        self.assertEqual(via["family"], "via")
        self.assertEqual(vial["family"], "vial")
        self.assertEqual(
            set(via["variants"]),
            {"via-release", "via-debug", "via-release-updater", "via-debug-updater"},
        )
        self.assertEqual(set(vial["variants"]), {"vial", "vial-updater"})
        self.assertTrue(set(via["variants"]).isdisjoint(vial["variants"]))

    def test_pull_requests_and_manual_runs_cannot_publish(self) -> None:
        for family in ("via", "vial"):
            with self.subTest(family=family):
                pull_request = self.release.plan(
                    "pull_request", "refs/pull/42/merge", f"release/{family}"
                )
                manual = self.release.plan(
                    "workflow_dispatch", f"refs/heads/release/{family}"
                )
                self.assertEqual(pull_request["family"], family)
                self.assertEqual(manual["family"], family)
                self.assertFalse(pull_request["publish"])
                self.assertFalse(manual["publish"])

    def test_version_tags_route_to_the_matching_family(self) -> None:
        self.assertEqual(
            self.release.plan("push", "refs/tags/via-v1.2.3")["family"], "via"
        )
        self.assertEqual(
            self.release.plan("push", "refs/tags/vial-4.5.6")["family"], "vial"
        )
        for invalid in ("v1.2.3", "via-vlatest", "vial-v1.2.3", "vial-01.2.3"):
            with self.subTest(tag=invalid), self.assertRaises(ValueError):
                self.release.plan("push", f"refs/tags/{invalid}")

    def test_main_builds_every_variant_without_publication(self) -> None:
        main = self.release.plan("push", "refs/heads/main")
        self.assertEqual(len(main["variants"]), 7)
        self.assertIn("qmk-default", main["variants"])
        self.assertFalse(main["publish"])

    def test_patch_versions_are_independent_and_sorted_numerically(self) -> None:
        tags = [
            ("via-v1.2.9", "old"),
            ("via-v1.2.10", "other"),
            ("vial-9.9.9", "other"),
            ("via-v01.2.99", "other"),
        ]
        self.assertEqual(self.release.choose_tag("via", tags, "new"), "via-v1.2.11")
        self.assertEqual(self.release.choose_tag("vial", tags, "new"), "vial-9.9.10")
        self.assertEqual(self.release.choose_tag("via", [], "new"), "via-v0.1.0")
        self.assertEqual(self.release.choose_tag("vial", [], "new"), "vial-0.1.0")

    def test_retry_reuses_tag_for_the_same_family_and_commit(self) -> None:
        tags = [("via-v0.1.0", "same"), ("vial-0.1.9", "same"), ("via-v0.1.3", "newer")]
        self.assertEqual(self.release.choose_tag("via", tags, "same"), "via-v0.1.0")

    def assets(self, family: str) -> Path:
        output = self.root / family
        output.mkdir()
        for variant in self.release.FAMILIES[family]:
            filename = f"gmk70-{variant}.tar.gz"
            content = f"firmware and source for {variant}".encode()
            (output / filename).write_bytes(content)
            digest = hashlib.sha256(content).hexdigest()
            (output / f"gmk70-{variant}.sha256").write_text(f"{digest}  {filename}\n")
        return output

    def test_verified_release_contains_only_selected_family(self) -> None:
        for family in ("via", "vial"):
            with self.subTest(family=family):
                output = self.assets(family)
                self.release.verify_assets(output, family)
                manifest = (output / "SHA256SUMS").read_text()
                self.assertEqual(
                    len(manifest.splitlines()), len(self.release.FAMILIES[family])
                )
                other = "vial" if family == "via" else "via-release"
                self.assertNotIn(f"gmk70-{other}.tar.gz", manifest)

    def test_mixed_family_artifacts_block_publication(self) -> None:
        output = self.assets("via")
        (output / "gmk70-vial.zip").write_text("wrong firmware family")
        with self.assertRaisesRegex(ValueError, "Unexpected release assets"):
            self.release.verify_assets(output, "via")
        self.assertFalse((output / "SHA256SUMS").exists())

    def test_corrupt_release_asset_blocks_publication(self) -> None:
        output = self.assets("vial")
        (output / "gmk70-vial.tar.gz").write_text("corrupt")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
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
        for family, expected_tag in (("via", "via-v0.1.0"), ("vial", "vial-0.1.0")):
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

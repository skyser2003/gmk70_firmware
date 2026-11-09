"""Route and publish independent, automatically versioned firmware releases."""

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path
from typing import TypedDict, cast

FAMILIES = {
    "via": ("via-release", "via-debug", "via-release-updater", "via-debug-updater"),
    "vial": ("vial", "vial-updater", "vial-unlocked-updater"),
    "zmk": ("zmk", "zmk-standalone"),
}
PREFIXES = {"via": "via-v", "vial": "vial-", "zmk": "zmk-v"}
TITLES = {"via": "VIA", "vial": "Vial", "zmk": "ZMK"}
VERSION = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"


class Plan(TypedDict):
    family: str
    variants: tuple[str, ...]
    publish: bool


class GitHubCommit(TypedDict):
    sha: str


class GitHubAsset(TypedDict):
    name: str


class GitHubRecord(TypedDict, total=False):
    name: str
    commit: GitHubCommit
    tag_name: str
    draft: bool
    assets: list[GitHubAsset]


def tag_version(family: str, tag: str) -> tuple[int, int, int] | None:
    match = re.fullmatch(re.escape(PREFIXES[family]) + VERSION, tag)
    if match is None:
        return None
    return int(match[1]), int(match[2]), int(match[3])


def plan(event: str, ref: str) -> Plan:
    if event == "push":
        for family, variants in FAMILIES.items():
            if ref == f"refs/heads/release/{family}":
                return {"family": family, "variants": variants, "publish": True}
    raise ValueError(
        "Only pushes to supported release branches can run firmware checks or builds"
    )


def choose_tag(family: str, tags: list[tuple[str, str]], commit: str) -> str:
    versions = [
        (version, tag, sha)
        for tag, sha in tags
        if (version := tag_version(family, tag)) is not None
    ]
    matching = [(version, tag) for version, tag, sha in versions if sha == commit]
    if matching:
        return max(matching)[1]
    if versions:
        major, minor, patch = max(version for version, _, _ in versions)
        version = (major, minor, patch + 1)
    else:
        version = (0, 1, 0)
    return PREFIXES[family] + ".".join(map(str, version))


def verify_assets(directory: Path, family: str) -> None:
    expected = {f"gmk70-{variant}.sha256" for variant in FAMILIES[family]}
    lines: list[str] = []
    for variant in FAMILIES[family]:
        manifest = directory / f"gmk70-{variant}.sha256"
        if manifest.is_symlink():
            raise ValueError(f"Redirected release manifest: {manifest.name}")
        contents = manifest.read_text().splitlines()
        names: set[str] = set()
        for line in contents:
            entry = re.fullmatch(r"([0-9a-f]{64})  (gmk70-[A-Za-z0-9_.-]+)", line)
            if (
                entry is None
                or not entry[2].startswith(f"gmk70-{variant}.")
                or entry[2] in names
            ):
                raise ValueError(f"Invalid release checksum entry: {line}")
            digest, name = entry.groups()
            if name.endswith(".hex"):
                raise ValueError("HEX firmware is no longer a supported release asset")
            path = directory / name
            if path.is_symlink() or not path.is_file():
                raise ValueError(f"Missing or redirected release asset: {name}")
            with path.open("rb") as handle:
                if hashlib.file_digest(handle, "sha256").hexdigest() != digest:
                    raise ValueError(f"Checksum mismatch: {name}")
            names.add(name)
            lines.append(line + "\n")
        if f"gmk70-{variant}.tar.gz" not in names:
            raise ValueError(f"Missing complete firmware/source bundle: {variant}")
        expected.update(names)
    unexpected = {path.name for path in directory.iterdir()} - expected - {"SHA256SUMS"}
    if unexpected:
        raise ValueError(f"Unexpected release assets: {', '.join(sorted(unexpected))}")
    (directory / "SHA256SUMS").write_text("".join(lines))


def github_pages(endpoint: str) -> list[GitHubRecord]:
    output = subprocess.check_output(
        ["gh", "api", "--paginate", "--slurp", endpoint], text=True
    )
    pages = cast(list[list[GitHubRecord]], json.loads(output))
    return [item for page in pages for item in page]


def run_gh(arguments: list[str]) -> None:
    subprocess.run(["gh", *arguments], check=True)


def publish(
    family: str, ref: str, commit: str, repository: str, directory: Path, notes: Path
) -> str:
    selected = plan("push", ref)
    if selected["family"] != family or not selected["publish"]:
        raise ValueError("Publication requires the matching release branch")
    if not re.fullmatch(r"[0-9a-f]{40}", commit) or not re.fullmatch(
        r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository
    ):
        raise ValueError("Invalid GitHub repository or commit")
    verify_assets(directory, family)
    tags = [
        (str(item["name"]), str(item["commit"]["sha"]))
        for item in github_pages(f"repos/{repository}/tags?per_page=100")
    ]
    tag = choose_tag(family, tags, commit)
    existing_sha = next((sha for name, sha in tags if name == tag), None)
    if existing_sha is not None and existing_sha != commit:
        raise ValueError(f"Release tag {tag} points to a different commit")
    release = next(
        (
            item
            for item in github_pages(f"repos/{repository}/releases?per_page=100")
            if item["tag_name"] == tag
        ),
        None,
    )
    if release is not None and not release["draft"]:
        if existing_sha is None:
            raise ValueError(f"Published release {tag} has no matching tag")
        print(f"Release {tag} is already published; preserving its assets")
        return tag
    if release is not None:
        allowed = {path.name for path in directory.iterdir()}
        if any(asset["name"] not in allowed for asset in release.get("assets", [])):
            raise ValueError(f"Draft {tag} contains unexpected release assets")
    if existing_sha is None:
        run_gh(
            [
                "api",
                "--method",
                "POST",
                f"repos/{repository}/git/refs",
                "-f",
                f"ref=refs/tags/{tag}",
                "-f",
                f"sha={commit}",
            ]
        )
    if release is None:
        run_gh(
            [
                "release",
                "create",
                tag,
                "--repo",
                repository,
                "--verify-tag",
                "--target",
                commit,
                "--draft",
                "--title",
                f"GMK70 {TITLES[family]} {tag}",
                "--notes-file",
                str(notes),
                "--generate-notes",
            ]
        )
    run_gh(
        [
            "release",
            "upload",
            tag,
            "--repo",
            repository,
            *[str(path) for path in sorted(directory.iterdir())],
            "--clobber",
        ]
    )
    run_gh(
        [
            "release",
            "edit",
            tag,
            "--repo",
            repository,
            "--draft=false",
            "--latest=false",
        ]
    )
    print(f"Published https://github.com/{repository}/releases/tag/{tag}")
    return tag


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("plan")
    release = commands.add_parser("publish")
    release.add_argument("--family", choices=tuple(FAMILIES), required=True)
    args = parser.parse_args()
    try:
        if args.command == "plan":
            result = plan(
                os.environ["GITHUB_EVENT_NAME"],
                os.environ["GITHUB_REF"],
            )
            print(f"family={result['family']}")
            print(f"variants={json.dumps(result['variants'])}")
            print(f"publish={str(result['publish']).lower()}")
        else:
            if os.environ["GITHUB_EVENT_NAME"] != "push":
                raise ValueError("Only push events publish releases")
            tag = publish(
                args.family,
                os.environ["GITHUB_REF"],
                os.environ["GITHUB_SHA"],
                os.environ["GITHUB_REPOSITORY"],
                Path("dist"),
                Path(f"docs/RELEASE-NOTES-{args.family}.md"),
            )
            if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
                with Path(summary).open("a") as handle:
                    handle.write(
                        f"Published [{tag}](https://github.com/{os.environ['GITHUB_REPOSITORY']}/releases/tag/{tag})\n"
                    )
    except (KeyError, OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Firmware release failed: {error}\n")


if __name__ == "__main__":
    main()

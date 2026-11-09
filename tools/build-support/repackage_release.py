"""Repackage an explicitly selected release without rebuilding its firmware."""

import argparse
import hashlib
import io
import json
import os
import subprocess
import tarfile
import tempfile
import zipfile
from pathlib import Path
from typing import Any

from firmware_assets import (
    UPDATE_FILES,
    asset_name,
    image_name,
    is_updater,
    write_package,
)
from github_release import (
    FAMILIES,
    github_pages,
    plan,
    tag_version,
    verify_assets,
    write_diff_notes,
)


def unpack_firmware(data: bytes, name: str, destination: Path) -> None:
    """Read only the four required regular members; never extract archive paths."""
    if name.endswith(".zip"):
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            members = archive.infolist()
            for required in UPDATE_FILES:
                matches = [item for item in members if item.filename == required]
                if len(matches) != 1 or matches[0].file_size > 1024 * 1024:
                    raise ValueError(
                        f"Missing, duplicate or oversized firmware: {required}"
                    )
                (destination / required).write_bytes(archive.read(matches[0]))
    else:
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
            for required in UPDATE_FILES:
                tar_matches = [
                    item for item in archive.getmembers() if item.name == required
                ]
                if (
                    len(tar_matches) != 1
                    or not tar_matches[0].isfile()
                    or tar_matches[0].size > 1024 * 1024
                ):
                    raise ValueError(
                        f"Missing, duplicate or oversized firmware: {required}"
                    )
                member = archive.extractfile(tar_matches[0])
                assert member is not None
                (destination / required).write_bytes(member.read())


def gh_json(*arguments: str) -> Any:
    return json.loads(subprocess.check_output(["gh", *arguments], text=True))


def repack(repository: str, family: str, tag: str, commit: str) -> None:
    if tag_version(family, tag) is None:
        raise ValueError("Repack tag does not match its firmware family")
    release = gh_json("api", f"repos/{repository}/releases/tags/{tag}")
    tags = [
        (str(item["name"]), str(item["commit"]["sha"]))
        for item in github_pages(f"repos/{repository}/tags?per_page=100")
    ]
    if release["draft"] or (tag, commit) not in tags:
        raise ValueError("Repack requires the exact published tag and commit")
    old_assets = {item["name"]: item for item in release["assets"]}
    with tempfile.TemporaryDirectory(prefix="gmk70-repack-") as temporary:
        root = Path(temporary)
        output = root / "dist"
        output.mkdir()

        def download(name: str) -> bytes:
            asset = old_assets[name]
            subprocess.run(
                [
                    "gh",
                    "release",
                    "download",
                    tag,
                    "--repo",
                    repository,
                    "--pattern",
                    name,
                    "--dir",
                    str(root),
                ],
                check=True,
            )
            data = (root / name).read_bytes()
            digest = asset.get("digest")
            if not digest or digest != "sha256:" + hashlib.sha256(data).hexdigest():
                raise ValueError(f"Downloaded asset hash mismatch: {name}")
            return data

        for variant in FAMILIES[family]:
            source = root / variant
            source.mkdir()
            if is_updater(variant):
                candidates = (
                    asset_name(variant),
                    f"gmk70-{variant}.zip",
                    f"gmk70-{variant}.tar.gz",
                )
                name = next((name for name in candidates if name in old_assets), None)
                if name is None:
                    raise ValueError(f"No existing firmware package for {variant}")
                unpack_firmware(download(name), name, source)
            else:
                (source / image_name(variant)).write_bytes(
                    download(asset_name(variant))
                )
            write_package(source, output, variant)
        verify_assets(output, family)
        notes = root / "notes.md"
        write_diff_notes(family, tag, commit, tags, notes)

        # Validate every replacement before writing; verify uploaded bytes before deletion.
        files = sorted(output.iterdir())
        subprocess.run(
            [
                "gh",
                "release",
                "upload",
                tag,
                "--repo",
                repository,
                "--clobber",
                *map(str, files),
            ],
            check=True,
        )
        fresh = gh_json("api", f"repos/{repository}/releases/tags/{tag}")
        uploaded = {item["name"]: item.get("digest") for item in fresh["assets"]}
        for path in files:
            if (
                uploaded.get(path.name)
                != "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()
            ):
                raise ValueError(f"Uploaded asset hash mismatch: {path.name}")
        for name in sorted(set(old_assets) - {path.name for path in files}):
            subprocess.run(
                [
                    "gh",
                    "release",
                    "delete-asset",
                    tag,
                    name,
                    "--repo",
                    repository,
                    "--yes",
                ],
                check=True,
            )
        subprocess.run(
            [
                "gh",
                "release",
                "edit",
                tag,
                "--repo",
                repository,
                "--notes-file",
                str(notes),
            ],
            check=True,
        )
        print(f"Repackaged {tag}: firmware bytes and tag unchanged")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family", choices=tuple(FAMILIES), required=True)
    args = parser.parse_args()
    selected = plan(os.environ["GITHUB_EVENT_NAME"], os.environ["GITHUB_REF"])
    if selected["family"] != args.family:
        raise ValueError("Repack requires the matching release branch")
    request = json.loads(Path(".github/repackage-releases.json").read_text())[
        args.family
    ]
    repack(
        os.environ["GITHUB_REPOSITORY"], args.family, request["tag"], request["commit"]
    )


if __name__ == "__main__":
    main()

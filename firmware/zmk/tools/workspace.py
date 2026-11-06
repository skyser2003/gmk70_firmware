"""Explicit pinned dependency setup and offline source verification for GMK70 ZMK."""

import argparse
import hashlib
import io
import json
import re
import subprocess
import tarfile
import tempfile
from pathlib import Path
from typing import TypedDict, cast


class Pin(TypedDict):
    repository: str
    commit: str


class ModulePin(Pin):
    path: str


class SourceLock(TypedDict):
    zmk: Pin
    zephyr: Pin
    zmk_modules: dict[str, ModulePin]


class SourceDigest(TypedDict):
    sha256: str
    link: str | None


class ExportManifest(TypedDict):
    pins: dict[str, str]
    files: dict[str, SourceDigest]


def sources(root: Path) -> list[tuple[Path, Pin]]:
    lock = cast(
        SourceLock, json.loads((root / "dependencies/sources.lock.json").read_text())
    )
    result = [
        (root / "dependencies/zmk", lock["zmk"]),
        (root / "dependencies/zephyr", lock["zephyr"]),
    ]
    result.extend(
        (root / "dependencies" / pin["path"], pin)
        for pin in lock["zmk_modules"].values()
    )
    return result


def git(path: Path, *arguments: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), *arguments], text=True
    ).strip()


def verify(root: Path) -> None:
    exported = root / "dependencies/zmk-source-files.json"
    if exported.is_file():
        if exported.is_symlink() or any(
            (path / ".git").exists() for path, _ in sources(root)
        ):
            raise ValueError(
                "Exported ZMK sources must not mix Git and archive identities"
            )
        if json.loads(exported.read_text()) != source_manifest(root):
            raise ValueError(
                "Exported ZMK dependency sources failed their integrity check"
            )
        return
    for path, pin in sources(root):
        if path.is_symlink() or not (path / ".git").exists():
            raise ValueError(f"Missing pinned {path.name}; run make setup-zmk")
        if git(path, "rev-parse", "HEAD") != pin["commit"]:
            raise ValueError(
                f"{path} does not match sources.lock.json; run make setup-zmk"
            )
        if git(path, "status", "--porcelain", "--untracked-files=all"):
            raise ValueError(f"{path} has local changes")


def setup(root: Path) -> None:
    if (root / "dependencies/zmk-source-files.json").exists():
        verify(root)
        return
    for path, pin in sources(root):
        if not re.fullmatch(r"[0-9a-f]{40}", pin["commit"]):
            raise ValueError(f"Invalid exact commit for {path}")
        if path.is_symlink():
            raise ValueError(f"Refusing redirected dependency {path}")
        if not path.exists():
            path.mkdir(parents=True)
            subprocess.run(["git", "init", "-q", str(path)], check=True)
            subprocess.run(
                ["git", "-C", str(path), "remote", "add", "origin", pin["repository"]],
                check=True,
            )
        if git(path, "status", "--porcelain", "--untracked-files=all"):
            raise ValueError(f"{path} has local changes")
        if git(path, "remote", "get-url", "origin") != pin["repository"]:
            raise ValueError(f"Unexpected source repository for {path}")
        subprocess.run(
            ["git", "-C", str(path), "fetch", "--depth=1", "origin", pin["commit"]],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(path), "checkout", "--detach", pin["commit"]], check=True
        )
    verify(root)


def source_manifest(root: Path) -> ExportManifest:
    result: ExportManifest = {"pins": {}, "files": {}}
    for directory, pin in sources(root):
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError(f"Missing or redirected source directory: {directory}")
        result["pins"][directory.relative_to(root).as_posix()] = pin["commit"]
        # Dependencies have already passed clean-tree verification. Git metadata
        # is never exported; preserve upstream internal links such as nanopb's.
        for path in sorted(directory.rglob("*")):
            if ".git" in path.relative_to(directory).parts:
                continue
            link = path.readlink() if path.is_symlink() else None
            if link is not None and (
                link.is_absolute()
                or not path.resolve().is_relative_to(directory.resolve())
            ):
                raise ValueError(f"Source link escapes its dependency: {path}")
            if path.is_dir() and link is None:
                continue
            result["files"][path.relative_to(root).as_posix()] = {
                "sha256": hashlib.sha256(
                    str(link).encode() if link is not None else path.read_bytes()
                ).hexdigest(),
                "link": str(link) if link is not None else None,
            }
    return result


def export(root: Path, archive: Path) -> None:
    """Extend the shared QMK/board source export with the verified ZMK inputs."""
    verify(root)
    manifest = source_manifest(root)
    if archive.is_symlink():
        raise ValueError("Refusing redirected source archive")
    with tempfile.TemporaryDirectory(
        prefix=".zmk-sources-", dir=archive.parent
    ) as temporary:
        output = Path(temporary) / archive.name
        with (
            tarfile.open(archive, "r:gz") as base,
            tarfile.open(output, "w:gz", compresslevel=1) as bundle,
        ):
            for entry in base:
                if (
                    entry.name in manifest["files"]
                    or entry.name == "dependencies/zmk-source-files.json"
                ):
                    raise ValueError(
                        "Source archive already contains ZMK dependency entries"
                    )
                content = base.extractfile(entry) if entry.isfile() else None
                try:
                    bundle.addfile(entry, content)
                finally:
                    if content is not None:
                        content.close()
            for name in manifest["files"]:
                bundle.add(root / name, arcname=name, recursive=False)
            payload = (json.dumps(manifest, indent=2) + "\n").encode()
            entry = tarfile.TarInfo("dependencies/zmk-source-files.json")
            entry.size = len(payload)
            bundle.addfile(entry, io.BytesIO(payload))
        output.replace(archive)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("setup", "verify", "export"))
    parser.add_argument("archive", nargs="?", type=Path)
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    try:
        if arguments.operation == "export":
            if arguments.archive is None:
                raise ValueError("export requires the shared source archive path")
            export(root, arguments.archive)
        else:
            (setup if arguments.operation == "setup" else verify)(root)
    except ValueError as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    main()

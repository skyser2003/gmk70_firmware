"""Validate pinned dependencies and compose disposable GMK70 build trees."""

import argparse
import configparser
import hashlib
import io
import json
import shutil
import subprocess
import tarfile
from pathlib import Path
from typing import TypedDict, cast

ROOT = Path(__file__).resolve().parents[2]
VARIANTS = ("default", "via")
IGNORED = {".git", ".venv", ".build", "__pycache__", ".ruff_cache", ".mypy_cache"}


class QmkPin(TypedDict):
    repository: str
    tag: str
    commit: str
    libraries: dict[str, str]


def pin(root: Path) -> QmkPin:
    data = json.loads((root / "dependencies/sources.lock.json").read_text())
    return cast(QmkPin, data["qmk"])


def git(directory: Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(directory), *args], text=True
    ).strip()


def source_files(directory: Path, excluded: set[Path] | None = None) -> list[Path]:
    result: list[Path] = []
    for path in sorted(directory.iterdir()):
        if path.name in IGNORED or (excluded and path in excluded):
            continue
        if path.is_symlink():
            raise ValueError(f"Source symlinks are not supported: {path}")
        if path.is_dir():
            result.extend(source_files(path, excluded))
        else:
            result.append(path)
    return result


def unused_modules(root: Path) -> set[Path]:
    qmk = root / "dependencies/qmk"
    selected = {qmk / path for path in pin(root)["libraries"]}
    excluded: set[Path] = set()
    for directory in (qmk, *selected):
        modules = configparser.ConfigParser()
        modules.read(directory / ".gitmodules")
        excluded.update(
            directory / modules[section]["path"] for section in modules.sections()
        )
    return excluded - selected


def dependency_hashes(root: Path) -> dict[str, str]:
    return {
        str(path.relative_to(root / "dependencies")): hashlib.sha256(
            path.read_bytes()
        ).hexdigest()
        for path in source_files(root / "dependencies/qmk", unused_modules(root))
    }


def verify(root: Path) -> None:
    locked = pin(root)
    qmk = root / "dependencies/qmk"
    if not (qmk / "quantum/quantum.c").is_file():
        raise ValueError("QMK sources are missing; run make setup")
    if (qmk / ".git").exists():
        for relative, revision in {
            ".": locked["commit"],
            **locked["libraries"],
        }.items():
            directory = qmk / relative
            if not (directory / ".git").exists():
                raise ValueError(f"Uninitialized dependency {relative}; run make setup")
            if git(directory, "rev-parse", "HEAD") != revision:
                raise ValueError(
                    f"Dependency {relative} does not match sources.lock.json"
                )
            if git(
                directory,
                "status",
                "--porcelain",
                "--untracked-files=all",
                "--ignore-submodules=all",
            ):
                raise ValueError(
                    f"Dependency {relative} has local changes; keep edits in firmware/"
                )
        if (
            git(qmk, "rev-parse", f"refs/tags/{locked['tag']}^{{commit}}")
            != locked["commit"]
        ):
            raise ValueError("QMK release tag does not match the pinned commit")
        for relative, revision in locked["libraries"].items():
            if git(qmk, "rev-parse", f"HEAD:{relative}") != revision:
                raise ValueError(
                    f"Library {relative} is not pinned by this QMK release"
                )
    else:
        manifest = root / "dependencies/source-files.json"
        if not manifest.is_file() or json.loads(
            manifest.read_text()
        ) != dependency_hashes(root):
            raise ValueError("Exported dependency sources failed their integrity check")


def generated_path(root: Path, *parts: str) -> Path:
    path = root / ".work"
    for part in ("", *parts):
        path = path / part
        if path.is_symlink() or path.resolve() != path.absolute():
            raise ValueError(f"Refusing to use redirected build directory: {path}")
    return path


def clean(root: Path) -> None:
    directory = generated_path(root, "qmk")
    if directory.exists():
        for child in directory.iterdir():
            if child.name not in VARIANTS or child.is_symlink():
                raise ValueError(f"Refusing to clean unexpected build path: {child}")
        shutil.rmtree(directory)


def compose(root: Path, variant: str) -> Path:
    if variant not in VARIANTS:
        raise ValueError(f"Unsupported QMK keymap: {variant}")
    verify(root)
    source = root / "dependencies/qmk"
    destination = generated_path(root, "qmk", variant)
    if destination.exists():
        shutil.rmtree(destination)
    unused = unused_modules(root)

    def ignore(directory: str, names: list[str]) -> set[str]:
        excluded = IGNORED.intersection(names)
        if Path(directory) == source:
            excluded.update({"keyboards", ".github"})
        excluded.update(name for name in names if Path(directory) / name in unused)
        return excluded

    shutil.copytree(source, destination, ignore=ignore)
    origins: dict[str, str] = {}
    overlays = (
        (root / "firmware/qmk-common", destination),
        (
            root / "firmware/qmk-via/keymaps",
            destination / "keyboards/zuoya/gmk70/keymaps",
        ),
        (root / "firmware/qmk-via/compat", destination / "keyboards/zuoya/gmk70"),
    )
    for overlay, target in overlays:
        if not overlay.exists():
            continue
        for path in source_files(overlay):
            output = target / path.relative_to(overlay)
            if output.exists():
                raise ValueError(
                    f"Overlay would overwrite an existing source: {output}"
                )
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, output)
            origins[str(output.relative_to(destination))] = str(path.relative_to(root))
    (destination / "gmk70-source-origins.json").write_text(
        json.dumps(origins, indent=2) + "\n"
    )
    return destination


def export_sources(root: Path, output: Path) -> None:
    verify(root)
    manifest = (json.dumps(dependency_hashes(root), indent=2) + "\n").encode()
    roots = (
        "firmware",
        "dependencies",
        "definitions",
        "scripts",
        "tests",
        "tools",
        "docs",
        ".github",
        "Makefile",
        "README.md",
        "LICENSE",
        "AGENTS.md",
        ".gitattributes",
        ".gitignore",
        ".gitmodules",
        ".clang-format",
    )
    with tarfile.open(output, "w:gz", compresslevel=1) as archive:
        for name in roots:
            path = root / name
            files = (
                source_files(path, unused_modules(root)) if path.is_dir() else [path]
            )
            for file in files:
                relative = str(file.relative_to(root))
                if relative != "dependencies/source-files.json":
                    archive.add(file, arcname=relative, recursive=False)
        entry = tarfile.TarInfo("dependencies/source-files.json")
        entry.size = len(manifest)
        archive.addfile(entry, io.BytesIO(manifest))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("verify", "compose", "clean", "export"))
    parser.add_argument("argument", nargs="?")
    args = parser.parse_args()
    try:
        match args.command:
            case "verify":
                verify(ROOT)
            case "compose":
                print(compose(ROOT, args.argument or "default"))
            case "clean":
                clean(ROOT)
            case "export":
                if not args.argument:
                    raise ValueError("export requires an output archive path")
                export_sources(ROOT, Path(args.argument))
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    main()

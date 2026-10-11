"""Validate pinned dependencies and prepare output-only GMK70 build directories."""

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
VARIANTS = ("default", "release", "debug")
LEGACY_VARIANTS = ("via", "via_diagnostic")
IGNORED = {".git", ".venv", ".build", "__pycache__", ".ruff_cache", ".mypy_cache"}


class QmkPin(TypedDict):
    repository: str
    tag: str
    commit: str
    libraries: dict[str, str]


def pin(root: Path, platform: str = "qmk") -> QmkPin:
    data = json.loads((root / "dependencies/sources.lock.json").read_text())
    return cast(QmkPin, data[platform])


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


def framework_path(root: Path, platform: str) -> Path:
    return root / "dependencies" / ("vial-qmk" if platform == "vial" else "qmk")


def unused_modules(root: Path, platform: str = "qmk") -> set[Path]:
    qmk = framework_path(root, platform)
    selected = {qmk / path for path in pin(root, platform)["libraries"]}
    excluded: set[Path] = set()
    for directory in (qmk, *selected):
        modules = configparser.ConfigParser()
        modules.read(directory / ".gitmodules")
        excluded.update(
            directory / modules[section]["path"] for section in modules.sections()
        )
    return excluded - selected


def dependency_hashes(root: Path, platform: str = "qmk") -> dict[str, str]:
    return {
        path.relative_to(root / "dependencies").as_posix(): hashlib.sha256(
            path.read_bytes()
        ).hexdigest()
        for path in source_files(
            framework_path(root, platform), unused_modules(root, platform)
        )
    }


def verify(root: Path, platform: str = "qmk") -> None:
    locked = pin(root, platform)
    qmk = framework_path(root, platform)
    setup_target = "setup-vial" if platform == "vial" else "setup"
    framework_name = "Vial" if platform == "vial" else "QMK"
    if not (qmk / "quantum/quantum.c").is_file():
        raise ValueError(
            f"{framework_name} sources are missing; run make {setup_target}"
        )
    if (qmk / ".git").exists():
        if platform == "qmk" and not locked["tag"]:
            raise ValueError("Official QMK requires a pinned release tag")
        for relative, revision in {
            ".": locked["commit"],
            **locked["libraries"],
        }.items():
            directory = qmk / relative
            if not (directory / ".git").exists():
                raise ValueError(
                    f"Uninitialized dependency {relative}; run make {setup_target}"
                )
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
        if locked["tag"] and (
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
        ) != dependency_hashes(root, platform):
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
            if child.name not in (*VARIANTS, *LEGACY_VARIANTS) or child.is_symlink():
                raise ValueError(f"Refusing to clean unexpected build path: {child}")
        shutil.rmtree(directory)
    vial_directory = generated_path(root, "vial")
    if vial_directory.exists():
        for child in vial_directory.iterdir():
            if child.name != "vial" or child.is_symlink():
                raise ValueError(f"Refusing to clean unexpected build path: {child}")
        shutil.rmtree(vial_directory)


def prepare(root: Path, variant: str, mode: str = "normal") -> Path:
    if variant not in VARIANTS:
        raise ValueError(f"Unsupported QMK keymap: {variant}")
    return prepare_framework(root, variant, "qmk", mode)


def prepare_vial(root: Path, mode: str = "normal") -> Path:
    return prepare_framework(root, "vial", "vial", mode)


def prepare_framework(root: Path, variant: str, platform: str, mode: str) -> Path:
    if mode not in ("normal", "updater"):
        raise ValueError(f"Unsupported build mode: {mode}")
    if variant == "default" and mode == "updater":
        raise ValueError("Updater supports release/debug/vial only")
    if platform == "qmk":
        verify(root)
    else:
        verify(root, platform)
    source = framework_path(root, platform)
    destination = generated_path(root, platform, variant, mode)
    generated_path(root, platform, variant, mode, ".build")
    origins: dict[str, str] = {}
    overlays = (
        (root / "firmware/qmk-common", Path(".")),
        (
            root / f"firmware/qmk-{'vial' if platform == 'vial' else 'via'}/keymaps",
            Path("keyboards/zuoya/gmk70/keymaps"),
        ),
        (
            root / f"firmware/qmk-{'vial' if platform == 'vial' else 'via'}/compat",
            Path("keyboards/zuoya/gmk70"),
        ),
    )
    for overlay, target in overlays:
        if not overlay.exists():
            continue
        for path in source_files(overlay):
            output = target / path.relative_to(overlay)
            relative = output.as_posix()
            if relative in origins or (source / output).exists():
                raise ValueError(
                    f"Overlay would overwrite an existing source: {output}"
                )
            origins[relative] = path.relative_to(root).as_posix()
    destination.mkdir(parents=True, exist_ok=True)
    manifest = destination / "gmk70-source-origins.json"
    if manifest.is_symlink():
        raise ValueError(f"Refusing to use redirected source manifest: {manifest}")
    content = json.dumps(origins, indent=2) + "\n"
    if not manifest.exists() or manifest.read_text() != content:
        manifest.write_text(content)
    return destination


def export_sources(root: Path, output: Path, platform: str = "qmk") -> None:
    verify(root, platform)
    manifest = (json.dumps(dependency_hashes(root, platform), indent=2) + "\n").encode()
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
                source_files(
                    path,
                    unused_modules(root, platform)
                    | {framework_path(root, "qmk" if platform == "vial" else "vial")},
                )
                if path.is_dir()
                else [path]
            )
            for file in files:
                relative = file.relative_to(root).as_posix()
                if relative != "dependencies/source-files.json":
                    archive.add(file, arcname=relative, recursive=False)
        entry = tarfile.TarInfo("dependencies/source-files.json")
        entry.size = len(manifest)
        archive.addfile(entry, io.BytesIO(manifest))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command",
        choices=(
            "verify",
            "prepare",
            "clean",
            "export",
            "verify-vial",
            "prepare-vial",
            "export-vial",
        ),
    )
    parser.add_argument("argument", nargs="?")
    parser.add_argument("--mode", choices=("normal", "updater"), default="normal")
    args = parser.parse_args()
    try:
        match args.command:
            case "verify":
                verify(ROOT)
            case "verify-vial":
                verify(ROOT, "vial")
            case "prepare-vial":
                print(prepare_vial(ROOT, args.mode))
            case "prepare":
                print(prepare(ROOT, args.argument or "release", args.mode))
            case "clean":
                clean(ROOT)
            case "export-vial":
                if not args.argument:
                    raise ValueError("export-vial requires an output archive path")
                export_sources(ROOT, Path(args.argument), "vial")
            case "export":
                if not args.argument:
                    raise ValueError("export requires an output archive path")
                export_sources(ROOT, Path(args.argument))
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    main()

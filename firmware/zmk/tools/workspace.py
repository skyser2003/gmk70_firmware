"""Explicit pinned dependency setup and offline source verification for GMK70 ZMK."""

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import TypedDict


class Pin(TypedDict):
    repository: str
    commit: str


def sources(root: Path) -> list[tuple[Path, Pin]]:
    lock = json.loads((root / "dependencies/sources.lock.json").read_text())
    result = [(root / "dependencies" / name, lock[name]) for name in ("zmk", "zephyr")]
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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("setup", "verify"))
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    try:
        (setup if arguments.operation == "setup" else verify)(root)
    except ValueError as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    main()

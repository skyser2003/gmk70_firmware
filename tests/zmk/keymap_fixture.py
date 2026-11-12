"""Read the maintained factory map with the selected board-profile aliases."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "firmware/zmk/boards/zuoya/gmk70_wired"


def read_layers(*, studio: bool = True) -> list[list[str]]:
    source = (BOARD / "gmk70_wired.keymap").read_text()
    profiles = [source]
    if studio:
        profiles.append((BOARD / "gmk70_studio.keymap").read_text())
    definitions: dict[str, str] = {}
    for profile in profiles:
        definitions.update(
            re.findall(r"^#\s*define\s+(GMK70_\w+_BINDING)\s+([^\n]+)", profile, re.M)
        )
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    source = "\n".join(line for line in source.splitlines() if not line.startswith("#"))
    source = re.sub(r"\bGMK70_\w+_BINDING\b", lambda m: definitions[m[0]], source)
    return [
        [" ".join(binding.split()) for binding in re.findall(r"&\w+[^&]*", layer)]
        for layer in re.findall(r"bindings\s*=\s*<(.*?)>;", source, re.S)
    ]

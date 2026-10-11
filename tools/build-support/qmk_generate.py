"""Run pinned QMK generators with external GMK70 keyboard/keymap discovery.

Only module-local path lookups change, within this process. Upstream generator
logic, schemas and mappings are used directly from the selected checkout.
"""

import importlib
import os
import sys
from pathlib import Path
from typing import Any, cast

GENERATORS = {
    "generate-rules-mk",
    "generate-config-h",
    "generate-keyboard-h",
    "generate-keyboard-c",
    "generate-make-dependencies",
    "generate-community-modules-rules-mk",
    "generate-community-post-config-h",
    "generate-community-modules-h",
    "generate-community-modules-c",
    "generate-community-modules-introspection-h",
    "generate-community-modules-introspection-c",
    "generate-led-matrix-community-modules-inc",
    "generate-rgb-matrix-community-modules-inc",
    "generate-split-transaction-id-community-modules-inc",
    "generate-version-h",
    "generate-rgb-breathe-table",
}


def main() -> None:
    if len(sys.argv) < 2 or sys.argv[1] not in GENERATORS:
        sys.exit("Only build metadata generators are supported by this adapter")
    root = Path(os.environ["GMK70_PROJECT_ROOT"])
    framework = Path.cwd()
    variant = os.environ["GMK70_KEYMAP"]
    platform = "vial" if framework.name == "vial-qmk" else "via"
    common = root / "firmware/qmk-common"
    keymap = (
        common / "keyboards/zuoya/gmk70/keymaps/default"
        if variant == "default"
        else root / f"firmware/qmk-{platform}/keymaps" / variant
    )
    os.environ["ORIG_CWD"] = str(framework)
    os.environ["QMK_HOME"] = str(framework)
    os.environ.pop("QMK_USERSPACE", None)
    # QMK Python imports must not write __pycache__ into pristine dependencies.
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(framework / "lib/python"))
    importlib.import_module("qmk_cli.script_qmk")

    def external_path(*parts: str | os.PathLike[str]) -> Path:
        path = Path(*parts)
        if path.parts and path.parts[0] == "keyboards":
            return common / path
        return path

    def is_keyboard(name: str) -> bool:
        return (
            name == "zuoya/gmk70"
            and (common / "keyboards" / name / "keyboard.json").is_file()
        )

    def locate_keymap(
        keyboard: str, name: str, force_layout: str | None = None
    ) -> Path | None:
        if keyboard == "zuoya/gmk70" and name == variant:
            return keymap / "keymap.c"
        return None

    # These pinned modules use Path('keyboards') for metadata traversal. Other
    # paths, including upstream data/mappings and data/schemas, stay unchanged.
    for name in ("qmk.keyboard", "qmk.info"):
        cast(Any, importlib.import_module(name)).Path = external_path
    cast(Any, importlib.import_module("qmk.path")).is_keyboard = is_keyboard
    cast(Any, importlib.import_module("qmk.keymap")).locate_keymap = locate_keymap
    importlib.import_module("qmk.cli")
    cast(
        Any, importlib.import_module("qmk.cli.generate.make_dependencies")
    ).Path = external_path
    result = importlib.import_module("milc").cli()
    if result is False:
        sys.exit(1)
    if isinstance(result, int) and not isinstance(result, bool):
        sys.exit(result)


if __name__ == "__main__":
    main()

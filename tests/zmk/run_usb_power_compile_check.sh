#!/usr/bin/env bash
# Reuse a wired build's pinned headers; no configure, firmware link or download.
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
firmware/zmk/tools/.venv/bin/python - "${1:-.work/zmk/wired-updater/updater/compile_commands.json}" <<'PY'
import json
from pathlib import Path
import shlex
import subprocess
import sys

root = Path.cwd()
work = root / ".work/zmk/usb-power-compile/normal"
work.mkdir(parents=True, exist_ok=True)
entries = json.loads(Path(sys.argv[1]).read_text())
entry = next(item for item in entries if item["file"].endswith("/soc.c"))
base = shlex.split(entry["command"])
for relative in ("gpio/gpio_wb32.c", "usb/device/usb_dc_wb32.c", "usb/device/usb_power_gmk70.c"):
    source = root / "firmware/zmk/drivers" / relative
    obj = work / f"{source.stem}.o"
    command = base.copy()
    command[command.index("-o") + 1] = str(obj)
    command[command.index("-c") + 1] = str(source)
    command.append("-Werror")
    subprocess.run(command, cwd=entry["directory"], check=True)
    if source.stem == "usb_power_gmk70":
        sections = subprocess.check_output(["readelf", "-SW", str(obj)], text=True)
        assert ".z_init_APPLICATION89_0_" in sections, sections
print("Real pinned Zephyr headers: GPIO/USB/B1 initializer ARM objects and APPLICATION 89 registration passed")
PY

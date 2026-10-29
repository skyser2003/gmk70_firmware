#!/usr/bin/env bash
# Compile real Cortex-M3 objects with pinned Zephyr headers, without downloads.
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
firmware/zmk/tools/.venv/bin/python - "${1:-.work/zmk/right-trial/normal/compile_commands.json}" <<'PY'
import json
from pathlib import Path
import shlex
import subprocess
import sys
root = Path.cwd()
work = root / ".work/zmk/receiver-compile/normal"
work.mkdir(parents=True, exist_ok=True)
entry = next(item for item in json.loads(Path(sys.argv[1]).read_text()) if item["file"].endswith("/soc.c"))
base = shlex.split(entry["command"])
update = root / "firmware/zmk/update"
common = root / "firmware/qmk-common/keyboards/zuoya/gmk70/update"
for profile in ("permanent", "qualification", "relay"):
    sources = [update / name for name in ("receiver.c", "raw_hid.c", "service.c", "wb32_flash.c")]
    sources += [common / "image.c", common / "protocol.c"]
    if profile == "relay":
        sources.append(common / "trial.c")
    for source in sources:
        command = base.copy()
        command[command.index("-o") + 1] = str(work / f"{source.stem}-{profile}.o")
        command[command.index("-c") + 1] = str(source)
        command.extend([f"-I{update}", f"-I{common}", f"-I{root / 'firmware/zmk/wired'}", f"-I{root / 'dependencies/zmk/app/include'}", "-Werror"])
        if profile == "qualification":
            command.append("-DCONFIG_GMK70_STAGING_QUALIFICATION=1")
        if profile == "relay":
            command.append("-DCONFIG_GMK70_ZMK_PEER_TRIAL_RELAY=1")
        subprocess.run(command, cwd=entry["directory"], check=True)
print("Real pinned Zephyr headers: permanent/qualification/relay receiver, HID, service, flash and direct common ARM objects passed")
PY

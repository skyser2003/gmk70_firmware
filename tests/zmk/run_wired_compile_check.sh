#!/usr/bin/env bash
# Reuse an existing pinned Zephyr compile database; no configure/download/link.
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
firmware/zmk/tools/.venv/bin/python - "${1:-.work/zmk/right-trial/normal/compile_commands.json}" <<'PY'
import json
from pathlib import Path
import shlex
import subprocess
import sys
root = Path.cwd()
work = root / ".work/zmk/wired-compile/normal"
work.mkdir(parents=True, exist_ok=True)
subprocess.run([str(root / "firmware/zmk/tools/.venv/bin/python"),
               str(root / "firmware/zmk/tools/wired_pins.py"), "--keyboard",
               str(root / "firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json"),
               "--output", str(work / "gmk70_wired_pins.h")], check=True)
entries = json.loads(Path(sys.argv[1]).read_text())
entry = next(item for item in entries if item["file"].endswith("/soc.c"))
base = shlex.split(entry["command"])
for source in ("wired/wired_protocol.c", "wired/wired_wb32.c", "drivers/kscan/kscan_gmk70_wired.c"):
    path = root / "firmware/zmk" / source
    # Kscan's DT device definition is the only board-specific compile dependency.
    # Validate its real API and implementation with pinned Zephyr headers while
    # Task3 supplies the new compatible node for the actual ARM link.
    if source.startswith("drivers/"):
        wrapper = work / "kscan_compile.c"
        wrapper.write_text('#include <zephyr/device.h>\n#undef DEVICE_DT_INST_DEFINE\n'
                           '#define DEVICE_DT_INST_DEFINE(...)\n'
                           f'#include "{path}"\n'
                           'static const void *const refs[] __attribute__((used)) = {&init, &api, &data};\n')
        path = wrapper
    command = base.copy()
    command[command.index("-o") + 1] = str(work / (Path(source).stem + ".o"))
    command[command.index("-c") + 1] = str(path)
    command.extend([f"-I{work}", f"-I{root / 'firmware/zmk/wired'}", "-Werror"])
    subprocess.run(command, cwd=entry["directory"], check=True)
print("Real pinned Zephyr headers: wired protocol, WB32 UART and kscan ARM object compile passed")
PY

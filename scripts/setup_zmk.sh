#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
python3 firmware/zmk/tools/workspace.py setup
uv sync --frozen --project firmware/zmk/tools
printf '\nPinned ZMK sources and build tools are ready.\n'

#!/usr/bin/env bash
# Explicit read-only device operation. Never invoked by build/test/setup.
set -euo pipefail
source "$(dirname "$0")/project.sh"
if [[ -n "${GMK70_DEVICE_PYTHON:-}" ]]; then
    exec "$GMK70_DEVICE_PYTHON" tools/firmware-update/read_eeprom.py "$@"
fi
exec uv run --offline --frozen --project tools/firmware-update python tools/firmware-update/read_eeprom.py "$@"

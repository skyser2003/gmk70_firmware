#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
find firmware/qmk-common firmware/qmk-via tests/qmk -name '*.[ch]' -print0 | xargs -0 clang-format -i
uv run --offline --frozen --project tools/build-support ruff format tools/build-support/*.py tests/repository tools/battery-diagnostics tools/firmware-update
uv run --offline --frozen --project tools/keymap-migration ruff format tools/keymap-migration

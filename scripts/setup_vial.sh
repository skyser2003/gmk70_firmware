#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
if [[ ! -f dependencies/source-files.json ]]; then
    git submodule update --init --depth 1 -- dependencies/vial-qmk
    git -C dependencies/vial-qmk submodule update --init --depth 1 -- lib/chibios lib/chibios-contrib lib/printf lib/lufa
fi
python3 tools/build-support/project_build.py verify-vial
printf 'Pinned Vial sources verified. Build tools use make setup; build firmware with make build-vial.\n'

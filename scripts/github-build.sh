#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
bash scripts/test.sh
mkdir -p dist
for keymap in default via; do
    bash scripts/build.sh qmk "$keymap"
    mkdir -p "dist/qmk/$keymap"
    cp artifacts/qmk/"$keymap"/* "dist/qmk/$keymap/"
done

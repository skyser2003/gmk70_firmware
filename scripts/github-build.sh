#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
bash scripts/test.sh
mkdir -p dist
for keymap in release debug; do
    bash scripts/build.sh qmk "$keymap"
    # GitHub release assets have a flat namespace: package shared manifest names.
    tar -czf "dist/zuoya_gmk70_$keymap.tar.gz" -C "artifacts/qmk/$keymap" .
    cp "artifacts/qmk/$keymap/zuoya_gmk70_$keymap.bin" "dist/"
    cp "artifacts/qmk/$keymap/zuoya_gmk70_$keymap.hex" "dist/"
done

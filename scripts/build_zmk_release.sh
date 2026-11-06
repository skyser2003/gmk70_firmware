#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"

# Setup and the reviewed resident prerequisite are explicit, separate operations.
# build_zmk.sh checks the resident identity and never rebuilds/replaces it.
bash scripts/build_zmk.sh wired-studio-updater
output="$project_root/artifacts/zmk/wired-studio-updater"
python3 tools/build-support/project_build.py export "$output/source.tar.gz"
python3 firmware/zmk/tools/workspace.py export "$output/source.tar.gz"
cp docs/RELEASE-NOTES-zmk.md "$output/README.txt"
(
    cd "$output"
    sha256sum application.bin resident.bin update.json package-info.json image-check.json README.txt > FIRMWARE-SHA256SUMS
    python3 -m zipfile -c gmk70-zmk.zip application.bin resident.bin update.json \
        package-info.json image-check.json README.txt FIRMWARE-SHA256SUMS
    sha256sum source.tar.gz README.txt FIRMWARE-SHA256SUMS gmk70-zmk.zip >> SHA256SUMS
)

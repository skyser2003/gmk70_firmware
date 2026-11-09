#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"

# Setup and the reviewed resident prerequisite are explicit, separate operations.
# build_zmk.sh checks the resident identity and never rebuilds/replaces it.
variant="${1:-wired-studio-updater}"
case "$variant" in
    wired-studio-updater) stem=gmk70-zmk ;;
    wired-studio-standalone) stem=gmk70-zmk-standalone ;;
    *) printf 'Unsupported ZMK release variant: %s\n' "$variant" >&2; exit 1 ;;
esac
bash scripts/build_zmk.sh "$variant"
output="$project_root/artifacts/zmk/$variant"
python3 tools/build-support/project_build.py export "$output/source.tar.gz"
python3 firmware/zmk/tools/workspace.py export "$output/source.tar.gz"
cp docs/RELEASE-NOTES-zmk.md "$output/README.txt"
(
    cd "$output"
    if [[ "$variant" == wired-studio-standalone ]]; then
        files=(zmk.bin image-check.json sources.lock.json build-info.txt README.txt)
    else
        files=(application.bin resident.bin install.bin update.json package-info.json image-check.json README.txt)
    fi
    sha256sum "${files[@]}" > FIRMWARE-SHA256SUMS
    python3 -m zipfile -c "$stem.zip" "${files[@]}" FIRMWARE-SHA256SUMS
    sha256sum source.tar.gz README.txt FIRMWARE-SHA256SUMS "$stem.zip" >> SHA256SUMS
)

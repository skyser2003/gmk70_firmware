#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"

# Setup and the reviewed updater resident remain separate operations.
case "${1:-wired-studio-updater}" in
    wired-studio-updater) variant=zmk ;;
    wired-studio-standalone) variant=zmk-standalone ;;
    *) printf 'Unsupported ZMK release variant: %s\n' "$1" >&2; exit 1 ;;
esac
exec bash scripts/github-build.sh "$variant"

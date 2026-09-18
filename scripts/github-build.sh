#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/test.sh
mkdir -p dist
for keymap in default via; do
    km="$keymap" bash scripts/build.sh
    mkdir -p "dist/$keymap"
    cp artifacts/"$keymap"/* "dist/$keymap/"
done

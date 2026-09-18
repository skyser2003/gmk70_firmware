#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
project_root="$(pwd -P)"
if [[ -f .env ]]; then
    set -a
    source .env
    set +a
elif [[ -f .envrc ]]; then
    source .envrc
fi
keymap="${km:-via}"
case "$keymap" in
    via|default|mine) ;;
    *) printf 'Unsupported keymap: %s\n' "$keymap" >&2; exit 1 ;;
esac
build_image='ghcr.io/qmk/qmk_cli@sha256:2dc05fc9f32efebd6b05c2b8676ee548358bc7e151e9dbf4dac6b6eed4513b07'
jobs="${JOBS:-8}"
git submodule update --init --recursive
mkdir -p qmk_firmware/keyboards/zuoya/gmk70 qmk_firmware/keyboards/linker/wireless "artifacts/$keymap"
rsync -a --delete keyboards/zuoya/gmk70/ qmk_firmware/keyboards/zuoya/gmk70/
rsync -a --delete keyboards/linker/wireless/ qmk_firmware/keyboards/linker/wireless/

# QMK's normal inner target avoids scanning every keyboard on a Windows mount.
# SKIP_VERSION removes wall-clock/Git probes; provenance is recorded below.
docker run --rm --mount "type=bind,src=$project_root,dst=/workspace" \
    --workdir /workspace/qmk_firmware "$build_image" \
    make -r -R -f builddefs/build_keyboard.mk -j"$jobs" \
    KEYBOARD=zuoya/gmk70 KEYMAP="$keymap" SKIP_VERSION=yes SILENT=false \
    2>&1 | tee "artifacts/$keymap/build.log"

target="zuoya_gmk70_$keymap"
for extension in bin hex elf map; do
    cp "qmk_firmware/.build/$target.$extension" "artifacts/$keymap/"
done
cp "qmk_firmware/.build/obj_$target/cflags.txt" "artifacts/$keymap/"
cp "qmk_firmware/.build/obj_$target/ldflags.txt" "artifacts/$keymap/"
{
    printf 'Keyboard: zuoya/gmk70\nKeymap: %s\nImage: %s\n' "$keymap" "$build_image"
    printf 'Source commit: '
    git rev-parse HEAD
    printf 'QMK and dependency commits:\n'
    git submodule status --recursive
    printf 'Local changes (part of this build):\n'
    git status --short --ignore-submodules=all
    printf 'Build option: SKIP_VERSION=yes (use this manifest for version information)\n'
} > "artifacts/$keymap/build-info.txt"
git diff --binary -- . ':!qmk_firmware' > "artifacts/$keymap/source-changes.patch"
find keyboards scripts tests -type f -print0 | sort -z | xargs -0 sha256sum > "artifacts/$keymap/source-sha256.txt"
tar -czf "artifacts/$keymap/source-overlay.tar.gz" keyboards scripts tests docs \
    Makefile README.md .gitmodules .gitattributes .clang-format .gitignore
(
    cd "artifacts/$keymap"
    sha256sum "$target.bin" "$target.hex" "$target.elf" "$target.map" > SHA256SUMS
)
printf '\nBuilt artifacts in %s/artifacts/%s\n' "$project_root" "$keymap"

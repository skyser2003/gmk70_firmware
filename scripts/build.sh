#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
platform="${1:-qmk}"
keymap="${2:-${km:-via}}"
case "$platform" in
    qmk) ;;
    vial) printf 'Vial is not implemented yet; see firmware/qmk-vial/README.md\n' >&2; exit 2 ;;
    zmk) printf 'ZMK is not implemented yet; see firmware/zmk/README.md\n' >&2; exit 2 ;;
    *) printf 'Unknown platform: %s\n' "$platform" >&2; exit 2 ;;
esac
case "$keymap" in
    via|default|via_diagnostic) ;;
    *) printf 'Unsupported QMK keymap: %s\n' "$keymap" >&2; exit 2 ;;
esac
for tool in arm-none-eabi-gcc qmk flock; do
    if ! command -v "$tool" >/dev/null; then
        printf 'Missing %s; run make setup first\n' "$tool" >&2
        exit 2
    fi
done
[[ ! -L .work && ! -L .work/locks ]] || { printf 'Redirected workspace directory\n' >&2; exit 2; }
mkdir -p .work/locks
exec 9>".work/locks/qmk-$keymap.lock"
flock 9
source_python="${GMK70_SOURCE_PYTHON:-python3}"
build_root="$("$source_python" tools/build-support/project_build.py compose "$keymap")"
output="artifacts/qmk/$keymap"
mkdir -p "$output"
export QMK_HOME="$build_root"
export QMK_USERSPACE=""
make -C "$build_root" -r -R -f builddefs/build_keyboard.mk -j"${JOBS:-8}" \
    KEYBOARD=zuoya/gmk70 KEYMAP="$keymap" SKIP_VERSION=yes SKIP_GIT=yes SILENT=false \
    2>&1 | tee "$output/build.log"
target="zuoya_gmk70_$keymap"
for extension in bin hex elf map; do
    cp "$build_root/.build/$target.$extension" "$output/"
done
arm-none-eabi-objcopy -I ihex -O binary "$output/$target.hex" "$output/from-hex.bin"
cmp "$output/$target.bin" "$output/from-hex.bin"
rm "$output/from-hex.bin"
cp "$build_root/.build/obj_$target/cflags.txt" "$output/"
cp "$build_root/.build/obj_$target/ldflags.txt" "$output/"
cp definitions/via/ZUOYA_GMK70.json dependencies/sources.lock.json "$output/"
cp "$build_root/gmk70-source-origins.json" "$output/"
{
    printf 'Platform: qmk\nKeyboard: zuoya/gmk70\nKeymap: %s\n' "$keymap"
    printf 'Custom keycode ABI: stock-v13 (see docs/STOCK-COMPATIBILITY.md)\n'
    printf 'Pinned sources:\n'
    cat dependencies/sources.lock.json
    arm-none-eabi-gcc --version
    arm-none-eabi-size -A "$output/$target.elf"
    qmk --version
    printf 'Complete working sources: source.tar.gz\n'
    printf 'Build option: SKIP_VERSION=yes (use this manifest for version information)\n'
} > "$output/build-info.txt"
"$source_python" tools/build-support/project_build.py export "$output/source.tar.gz"
# Remove obsolete manifests from builds made before the upstream migration.
rm -f "$output/UPSTREAM-SOURCES.tsv" "$output/source-sha256.txt"
(
    cd "$output"
    sha256sum "$target.bin" "$target.hex" "$target.elf" "$target.map" ZUOYA_GMK70.json \
        sources.lock.json gmk70-source-origins.json build-info.txt cflags.txt ldflags.txt source.tar.gz > SHA256SUMS
)
printf '\nBuilt artifacts in %s/%s\n' "$project_root" "$output"

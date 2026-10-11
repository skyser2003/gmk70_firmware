#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
platform="${1:-qmk}"
keymap="${2:-${km:-release}}"
mode="${3:-normal}"
case "$mode" in
    normal|updater) ;;
    *) printf 'Unknown build mode: %s\n' "$mode" >&2; exit 2 ;;
esac
case "$platform" in
    qmk) ;;
    vial) exec bash ./scripts/build_vial.sh "${2:-vial}" "$mode" ;;
    zmk) printf 'ZMK is not implemented yet; see firmware/zmk/README.md\n' >&2; exit 2 ;;
    *) printf 'Unknown platform: %s\n' "$platform" >&2; exit 2 ;;
esac
case "$keymap" in
    via) keymap=release ;;
    via_diagnostic) keymap=debug ;;
    release|debug|default) ;;
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
build_root="$("$source_python" tools/build-support/project_build.py prepare "$keymap" --mode "$mode")"
output="artifacts/qmk/$keymap"
update_flags=()
if [[ "$mode" == updater ]]; then
    [[ "$keymap" != default ]] || { printf 'Updater supports release/debug only\n' >&2; exit 2; }
    output="$output-updater"
    update_flags+=(GMK70_SPLIT_UPDATE=yes MCU_LDSCRIPT=GMK70_UPDATE USE_CHIBIOS_CONTRIB=yes)
fi
mkdir -p "$output"
export QMK_HOME="$qmk_root"
export QMK_USERSPACE=""
export GMK70_PROJECT_ROOT="$project_root" GMK70_KEYMAP="$keymap" PYTHONDONTWRITEBYTECODE=1
target="zuoya_gmk70_$keymap"
make -C "$qmk_root" -r -R -f "$project_root/tools/build-support/external.mk" -j"${JOBS:-8}" \
    BUILD_DIR="$build_root/.build" GMK70_PLATFORM=qmk \
    QMK_BIN="python3 $project_root/tools/build-support/qmk_generate.py" \
    KEYBOARD=zuoya/gmk70 KEYMAP="$keymap" SKIP_VERSION=yes SKIP_GIT=yes SILENT=false \
    "${update_flags[@]}" \
    elf hex "$build_root/.build/$target.bin" sizeafter \
    2>&1 | tee "$output/build.log"
for extension in bin hex elf map; do
    cp "$build_root/.build/$target.$extension" "$output/"
done
arm-none-eabi-objcopy -I ihex -O binary "$output/$target.hex" "$output/from-hex.bin"
cmp "$output/$target.bin" "$output/from-hex.bin"
rm "$output/from-hex.bin"
cp "$build_root/.build/obj_$target/cflags.txt" "$output/"
cp "$build_root/.build/obj_$target/ldflags.txt" "$output/"
if [[ "$mode" == updater ]]; then
    python3 tools/build-support/update_package.py "$qmk_root" "$output" "$keymap"
fi
# Enforce the distribution boundary against the actual compiled adapter.
if [[ "$keymap" != default ]]; then
    bootloader_symbols="$(arm-none-eabi-nm -u "$build_root/.build/obj_$target/stock_via.o")"
    grep -q ' gmk70_stock_request_bootloader$' <<< "$bootloader_symbols"
    if [[ "$keymap" == debug ]]; then
        grep -q -- '-DGMK70_DEBUG_USB_BOOTLOADER' "$output/cflags.txt"
    elif grep -q -- '-DGMK70_DEBUG_USB_BOOTLOADER\|-DGMK70_SPLIT_DIAGNOSTICS' "$output/cflags.txt" || grep -q ' reset_keyboard$' <<< "$bootloader_symbols"; then
        printf 'Release unexpectedly contains debug features\n' >&2
        exit 1
    fi
fi
cp definitions/via/ZUOYA_GMK70.json dependencies/sources.lock.json "$output/"
cp "$build_root/gmk70-source-origins.json" "$output/"
{
    printf 'Platform: qmk\nKeyboard: zuoya/gmk70\nKeymap: %s\n' "$keymap"
    printf 'Build mode: %s\n' "$mode"
    if [[ "$mode" == updater ]]; then
        printf 'EXPERIMENTAL: initial direct-USB install.bin for each half; subsequent updates use application.bin + update.json. Requires verified 256 KB MCU. Hardware validation pending.\n'
    fi
    if [[ "$keymap" == debug ]]; then
        printf 'Debug features: split RAM logger; optional direct USB split updater\n'
        if [[ "$mode" == updater ]]; then
            printf 'Persistent sleep records: external SPI journal on both halves; read with read_sleep_log.py (see docs/PERSISTENT-SLEEP-LOG.md)\n'
        fi
    else
        printf 'Debug features: disabled\n'
    fi
    printf 'Behavior target: stock V15 (see docs/STOCK-V15-RECONSTRUCTION.md)\n'
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
    images=("$target.bin" "$target.hex")
    if [[ "$mode" == updater ]]; then
        images=(application.bin application.hex resident.bin resident.elf resident.map install.bin install.hex update.json)
    fi
    sha256sum "${images[@]}" "$target.elf" "$target.map" ZUOYA_GMK70.json \
        sources.lock.json gmk70-source-origins.json build-info.txt cflags.txt ldflags.txt source.tar.gz > SHA256SUMS
)
printf '\nBuilt artifacts in %s/%s\n' "$project_root" "$output"

#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
[[ "${1:-vial}" == vial && "${2:-normal}" == normal ]] || {
    printf 'Only the Vial keymap and normal build are implemented; Vial updater/debug builds are pending.\n' >&2
    exit 2
}
for tool in arm-none-eabi-gcc qmk flock; do
    command -v "$tool" >/dev/null || { printf 'Missing %s; run make setup and make setup-vial\n' "$tool" >&2; exit 2; }
done
[[ ! -L .work && ! -L .work/locks ]] || exit 2
mkdir -p .work/locks artifacts/vial/vial
exec 9>.work/locks/vial-vial.lock
flock 9
source_python="${GMK70_SOURCE_PYTHON:-python3}"
build_root="$("$source_python" tools/build-support/project_build.py compose-vial)"
output=artifacts/vial/vial
export QMK_HOME="$build_root" QMK_USERSPACE=""
make -C "$build_root" -r -R -f builddefs/build_keyboard.mk -j"${JOBS:-8}" \
    KEYBOARD=zuoya/gmk70 KEYMAP=vial SKIP_VERSION=yes SKIP_GIT=yes SILENT=false DEBUG_ENABLE=yes \
    2>&1 | tee "$output/build.log"
target=zuoya_gmk70_vial
for extension in bin hex elf map; do
    cp "$build_root/.build/$target.$extension" "$output/"
done
arm-none-eabi-objcopy -I ihex -O binary "$output/$target.hex" "$output/from-hex.bin"
cmp "$output/$target.bin" "$output/from-hex.bin"
rm "$output/from-hex.bin"
cp "$build_root/.build/obj_$target/"{cflags,ldflags}.txt "$output/"
grep -q -- '-DVIAL_ENABLE' "$output/cflags.txt"
for feature in VIALRGB_ENABLE QMK_SETTINGS TAP_DANCE_ENABLE COMBO_ENABLE KEY_OVERRIDE_ENABLE \
    CAPS_WORD_ENABLE LAYER_LOCK_ENABLE REPEAT_KEY_ENABLE AUTO_SHIFT_ENABLE MOUSEKEY_ENABLE \
    TAPPING_TERM_PER_KEY QUICK_TAP_TERM_PER_KEY CHORDAL_HOLD FLOW_TAP_TERM; do
    grep -q -- "-D$feature" "$output/cflags.txt" || {
        printf 'Missing Vial feature: %s\n' "$feature" >&2; exit 1;
    }
done
if grep -q -- '-DVIAL_INSECURE' "$output/cflags.txt"; then
    printf 'Vial physical unlock was disabled\n' >&2; exit 1
fi
cp firmware/qmk-vial/keymaps/vial/vial.json dependencies/sources.lock.json "$output/"
cp "$build_root/gmk70-source-origins.json" "$output/"
{
    printf 'Platform: vial\nKeyboard: zuoya/gmk70\nKeymap: vial\n'
    printf 'EXPERIMENTAL port: no hardware validation. Normal image, not an existing resident-updater application.\n'
    printf 'Storage: native Vial layout, schema 0x564D4702 at 0xFFC. Stock/prior Vial EEPROM data is not imported; back up settings before installation.\n'
    printf 'Unlock: Escape + left Shift. Four layers, one encoder, 16 macros (2123 bytes).\n'
    printf 'Dynamic entries: 32 tap dances, 32 combos, 32 key overrides, 32 alternate-repeat rules.\n'
    printf 'Features: live QMK settings (Auto Shift, tap/hold, Quick Tap, Flow Tap, one-shots, mouse keys), Caps Word, Layer Lock, Repeat/Alternate Repeat, extended macros, VialRGB/direct LED control.\n'
    printf 'Defaults: empty dynamic tables; Auto Shift and Flow Tap off until configured in Vial. Vial updater integration is pending.\n'
    cat dependencies/sources.lock.json
    arm-none-eabi-gcc --version
    arm-none-eabi-size -A "$output/$target.elf"
    printf 'Complete working sources: source.tar.gz\n'
} > "$output/build-info.txt"
cat > "$output/README.txt" <<'EOF'
GMK70 experimental Vial firmware

zuoya_gmk70_vial.hex and .bin are the same normal image, based at 0x08000000.
This is NOT application.bin for the existing resident split updater. Do not
submit this image to that updater. Vial updater integration is pending.
Both halves must use this same build. Physical installation and testing have
not been performed. No build or test command flashes a keyboard.

Back up current mappings and settings before installation. Native Vial storage
has schema 0x564D4702; stock/VIA and the earlier Vial baseline are reset to Vial
defaults. No automatic settings migration is provided.

Use the Vial GUI. Identification/layout is embedded; vial.json is also included.
Unlock by holding Escape + left Shift when prompted by Vial.
All discussed advanced features are compiled. Dynamic tables start empty;
assign/configure them in Vial. Auto Shift and Flow Tap are initially off.
Four layers, one encoder, 16 macros with 2123 shared bytes; 32 entries each for
tap dances, combos, key overrides and alternate-repeat rules.

See build-info.txt for feature details and dependency pins. Verify this package
with: sha256sum -c FIRMWARE-SHA256SUMS
Complete sources, ELF/map and full checksums are exported beside this package.
EOF
"$source_python" tools/build-support/project_build.py export-vial "$output/source.tar.gz"
(
    cd "$output"
    sha256sum "$target.bin" "$target.hex" vial.json build-info.txt README.txt > FIRMWARE-SHA256SUMS
    "$source_python" -m zipfile -c gmk70-vial.zip "$target.bin" "$target.hex" vial.json \
        build-info.txt README.txt FIRMWARE-SHA256SUMS
)
(
    cd "$output"
    sha256sum "$target.bin" "$target.hex" "$target.elf" "$target.map" vial.json \
        sources.lock.json gmk70-source-origins.json cflags.txt ldflags.txt build-info.txt source.tar.gz \
        README.txt FIRMWARE-SHA256SUMS gmk70-vial.zip > SHA256SUMS
)
printf '\nBuilt experimental Vial artifacts in %s/%s\n' "$project_root" "$output"

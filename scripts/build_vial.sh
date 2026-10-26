#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
mode="${2:-normal}"
[[ "${1:-vial}" == vial && ( "$mode" == normal || "$mode" == updater || "$mode" == radio-diagnostic-updater || "$mode" == trial-updater ) ]] || {
    printf 'Vial supports normal, updater, radio-diagnostic-updater, or trial-updater mode.\n' >&2
    exit 2
}
for tool in arm-none-eabi-gcc qmk flock; do
    command -v "$tool" >/dev/null || { printf 'Missing %s; run make setup and make setup-vial\n' "$tool" >&2; exit 2; }
done
[[ ! -L .work && ! -L .work/locks ]] || exit 2
mkdir -p .work/locks
exec 9>.work/locks/vial-vial.lock
flock 9
source_python="${GMK70_SOURCE_PYTHON:-python3}"
build_root="$("$source_python" tools/build-support/project_build.py prepare-vial --mode "$mode")"
framework_root="$project_root/dependencies/vial-qmk"
output=artifacts/vial/vial
update_flags=()
if [[ "$mode" != normal ]]; then
    if [[ "$mode" == radio-diagnostic-updater ]]; then
        output="$output-radio-diagnostic-updater"
        update_flags+=(GMK70_RADIO_DIAGNOSTICS=yes)
    elif [[ "$mode" == trial-updater ]]; then
        output="$output-trial-updater"
        update_flags+=(GMK70_VIAL_TRIAL=yes)
    else
        output="$output-updater"
    fi
    update_flags+=(GMK70_SPLIT_UPDATE=yes MCU_LDSCRIPT=GMK70_UPDATE USE_CHIBIOS_CONTRIB=yes)
fi
mkdir -p "$output"
export QMK_HOME="$framework_root" QMK_USERSPACE=""
export GMK70_PROJECT_ROOT="$project_root" GMK70_KEYMAP=vial PYTHONDONTWRITEBYTECODE=1
target=zuoya_gmk70_vial
make -C "$framework_root" -r -R -f "$project_root/tools/build-support/external.mk" -j"${JOBS:-8}" \
    BUILD_DIR="$build_root/.build" GMK70_PLATFORM=vial \
    QMK_BIN="python3 $project_root/tools/build-support/qmk_generate.py" \
    KEYBOARD=zuoya/gmk70 KEYMAP=vial SKIP_VERSION=yes SKIP_GIT=yes SILENT=false DEBUG_ENABLE=yes \
    "${update_flags[@]}" \
    elf hex "$build_root/.build/$target.bin" sizeafter \
    2>&1 | tee "$output/build.log"
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
if grep -q -- '-DGMK70_DEBUG_USB_BOOTLOADER\|-DGMK70_SPLIT_DIAGNOSTICS\|-DGMK70_PERSISTENT_DIAGNOSTICS' "$output/cflags.txt"; then
    printf 'Vial unexpectedly contains diagnostics\n' >&2; exit 1
fi
if [[ "$mode" == radio-diagnostic-updater ]]; then
    grep -q -- '-DGMK70_RADIO_DIAGNOSTICS' "$output/cflags.txt"
else
    if grep -q -- '-DGMK70_RADIO_DIAGNOSTICS' "$output/cflags.txt"; then
        printf 'Normal Vial unexpectedly contains radio capture\n' >&2; exit 1
    fi
fi
if [[ "$mode" == trial-updater ]]; then
    grep -q -- '-DGMK70_VIAL_TRIAL' "$output/cflags.txt"
    arm-none-eabi-nm "$output/$target.elf" > "$output/trial-symbols.txt"
    grep -q ' gmk_trial_process$' "$output/trial-symbols.txt"
    rm "$output/trial-symbols.txt"
else
    if grep -q -- '-DGMK70_VIAL_TRIAL' "$output/cflags.txt"; then
        printf 'Normal Vial unexpectedly contains the trial receiver\n' >&2; exit 1
    fi
fi
if [[ "$mode" != normal ]]; then
    grep -q -- '-DGMK70_SPLIT_UPDATE' "$output/cflags.txt"
    grep -q -- '-DGMK70_VIAL_UPDATE' "$output/cflags.txt"
    if arm-none-eabi-nm "$output/$target.elf" | grep ' gmk_eeprom_query\| gmk_sleep_log_query' >/dev/null; then
        printf 'Vial updater unexpectedly contains settings/log queries\n' >&2; exit 1
    fi
    "$source_python" tools/build-support/update_package.py "$framework_root" "$output" vial
    cp tools/firmware-update/update.py "$output/"
    if [[ "$mode" == radio-diagnostic-updater ]]; then
        cp tools/firmware-update/read_radio.py "$output/"
    fi
    printf 'hidapi==0.15.0\n' > "$output/requirements.txt"
fi
cp firmware/qmk-vial/keymaps/vial/vial.json dependencies/sources.lock.json "$output/"
cp "$build_root/gmk70-source-origins.json" "$output/"
{
    printf 'Platform: vial\nKeyboard: zuoya/gmk70\nKeymap: vial\n'
    printf 'Build mode: %s\nEXPERIMENTAL port: no hardware validation.\n' "$mode"
    if [[ "$mode" != normal ]]; then
        printf 'Updater: ABI 1, existing resident installer; direct USB left master relays to right slave. Firmware writes require Vial physical unlock. EEPROM queries and persistent logs disabled.\n'
    else
        printf 'Normal image; use the separate vial-updater package with the resident split updater.\n'
    fi
    if [[ "$mode" == radio-diagnostic-updater ]]; then
        printf 'Exception: optional RAM radio counters and read-only USB snapshot enabled. Matching native storage schema retains installed EEPROM magic; no per-build settings reset.\n'
    fi
    printf 'Storage: native Vial layout, schema 0x564D4702 at 0xFFC. Stock/prior Vial EEPROM data is not imported; back up settings before installation.\n'
    printf 'Unlock: Escape + left Shift. Four layers, one encoder, 16 macros (2123 bytes).\n'
    printf 'Dynamic entries: 32 tap dances, 32 combos, 32 key overrides, 32 alternate-repeat rules.\n'
    printf 'Features: live QMK settings (Auto Shift, tap/hold, Quick Tap, Flow Tap, one-shots, mouse keys), Caps Word, Layer Lock, Repeat/Alternate Repeat, extended macros, VialRGB/direct LED control.\n'
    printf 'Defaults: empty dynamic tables; Auto Shift and Flow Tap off until configured in Vial.\n'
    cat dependencies/sources.lock.json
    arm-none-eabi-gcc --version
    arm-none-eabi-size -A "$output/$target.elf"
    printf 'Complete working sources: source.tar.gz\n'
} > "$output/build-info.txt"
cat > "$output/README.txt" <<'EOF'
GMK70 experimental Vial firmware

Both halves must use this same build. Physical installation and testing have
not been performed. No build or test command flashes a keyboard.

Back up current mappings and settings before installation. Native Vial storage
has schema 0x564D4702; stock/VIA and the earlier Vial baseline are reset to Vial
defaults. No automatic settings migration is provided.
Native Vial generates a new EEPROM magic per build: installing a rebuilt image
resets dynamic mappings and macros even when the layout schema is unchanged.
Export your configuration in the Vial GUI before updating.

Use the Vial GUI. Identification/layout is embedded; vial.json is also included.
Unlock by holding Escape + left Shift when prompted by Vial.
Knob rotation: in Keymap, choose a layer and either circular control above the
top-left knob button. Left is counterclockwise, right is clockwise. Assign any
supported keycode; the knob's press action is a separate rectangular button.
All discussed advanced features are compiled. Dynamic tables start empty;
assign/configure them in Vial. Auto Shift and Flow Tap are initially off.
Four layers, one encoder, 16 macros with 2123 shared bytes; 32 entries each for
tap dances, combos, key overrides and alternate-repeat rules.

See build-info.txt for feature details and dependency pins. Verify this package
with: sha256sum -c FIRMWARE-SHA256SUMS
Complete sources, ELF/map and full checksums are exported beside this package.
EOF
images=("$target.bin" "$target.hex")
extras=()
support=()
archive=gmk70-vial.zip
if [[ "$mode" != normal ]]; then
    archive=gmk70-vial-updater.zip
    images=(application.bin application.hex resident.bin install.bin install.hex update.json)
    extras=(update.py requirements.txt)
    if [[ "$mode" == radio-diagnostic-updater ]]; then
        archive=gmk70-vial-radio-diagnostic-updater.zip
        extras+=(read_radio.py)
    fi
    if [[ "$mode" == trial-updater ]]; then
        archive=gmk70-vial-trial-updater.zip
        cp docs/ZMK-TRIAL-LAUNCHER.md "$output/TRIAL-LAUNCHER.md"
        cp tools/firmware-update/trial.py "$output/"
        extras+=(TRIAL-LAUNCHER.md trial.py)
    fi
    support=(resident.elf resident.map)
    cat >> "$output/README.txt" <<'EOF'

Updater images:
- application.bin/.hex at 0x08002000: subsequent resident split updates.
- update.json + resident.bin: checked by update.py before any write.
- install.bin/.hex at 0x08000000: initial direct installation on each half,
  requiring a verified 256 KB WB32FQ95 MCU. Do not use as application.bin.

With an existing compatible debug USB updater, select wired mode, connect only
the left USB half, keep the right powered and linked, and close VIA/Vial.
For subsequent Vial updates, first unlock in the Vial GUI (Escape + left Shift),
then close Vial without unplugging or resetting the keyboard.
Install Python dependencies: python -m pip install -r requirements.txt
Read-only check: python update.py . --target both --inspect
Explicit installation: python update.py . --target both --install
The tool checks both resident installers, updates/verifies right, then left.
It refuses an incompatible resident CRC. Settings are reset when migrating
from stock/VIA; normal Vial and Vial updater share the same settings schema,
but native Vial's per-build magic still resets dynamic mappings and macros.
EEPROM backups, RAM diagnostics and persistent logging are not included.
EOF
else
    cat >> "$output/README.txt" <<'EOF'

zuoya_gmk70_vial.hex and .bin are the same normal image at 0x08000000.
Use the separate gmk70-vial-updater.zip for the resident split updater.
Do not submit this normal image as application.bin to that updater.
EOF
fi
if [[ "$mode" == trial-updater ]]; then
    cat >> "$output/README.txt" <<'EOF_TRIAL'

Experimental staging trial receiver (namespace 0x71).
RUN is disabled: no watchdog/startup handoff backend is implemented or proven.
This is an updater application, NOT a staging trial binary. Its ordinary update
package still installs at 0x08002000 and keeps the existing resident unchanged.
Trial fixtures use separate trial.json/trial.bin packages. Never INSTALL them.
Use Python 3.12+ and the included dependency-free trial.py to inspect or plan
trial packages offline. It never opens USB and has no RUN command.
See TRIAL-LAUNCHER.md for the recovery design and remaining hardware proof.
EOF_TRIAL
fi
if [[ "$mode" == radio-diagnostic-updater ]]; then
    cp docs/RADIO-DIAGNOSTICS.md "$output/README.txt"
fi
"$source_python" tools/build-support/project_build.py export-vial "$output/source.tar.gz"
(
    cd "$output"
    sha256sum "${images[@]}" "${extras[@]}" vial.json build-info.txt README.txt > FIRMWARE-SHA256SUMS
    "$source_python" -m zipfile -c "$archive" "${images[@]}" "${extras[@]}" vial.json \
        build-info.txt README.txt FIRMWARE-SHA256SUMS
)
(
    cd "$output"
    sha256sum "${images[@]}" "${extras[@]}" "${support[@]}" "$target.elf" "$target.map" vial.json \
        sources.lock.json gmk70-source-origins.json cflags.txt ldflags.txt build-info.txt source.tar.gz \
        README.txt FIRMWARE-SHA256SUMS "$archive" > SHA256SUMS
)
printf '\nBuilt experimental Vial artifacts in %s/%s\n' "$project_root" "$output"

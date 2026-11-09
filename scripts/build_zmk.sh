#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
variant="${1:-left-trial}"
case "$variant" in
    wired-studio-handoff|wired-studio-watchdog-rollback)
        printf 'Retired experiment %s; building canonical no-watchdog Studio.\n' "$variant"
        variant=wired-studio-updater ;;
    wired-studio-handoff-test|wired-studio-watchdog-rollback-test)
        printf 'Retired experiment %s; building canonical no-watchdog Studio test.\n' "$variant"
        variant=wired-studio-update-test ;;
esac
board=gmk70_wired
mode=updater
extra=()
case "$variant" in
    left-trial|right-trial) board="gmk70_${variant%-trial}"; mode=normal ;;
    wired-updater) ;;
    wired-peripheral) extra+=(-DEXTRA_CONF_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/peripheral.conf") ;;
    wired-update-test) extra+=(-DEXTRA_CONF_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/update-test.conf") ;;
    wired-studio-updater|wired-studio-standalone|wired-studio-update-test|wired-studio-radio-qualification|wired-studio-native-bridge) extra+=(-DGMK70_STUDIO=ON
        -DEXTRA_CONF_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/studio.conf"
        -DDTC_OVERLAY_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/studio.overlay") ;;
    wired-qualification) extra+=(-DEXTRA_CONF_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/qualification.conf"
        -DDTC_OVERLAY_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/qualification.overlay") ;;
    *) printf 'Unsupported experimental ZMK target: %s\n' "$variant" >&2; exit 2 ;;
esac
if [[ "$variant" == wired-studio-standalone ]]; then
    mode=normal
    extra+=(-DEXTRA_CONF_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/studio.conf;$project_root/firmware/zmk/boards/zuoya/gmk70_wired/standalone.conf"
        -DDTC_OVERLAY_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/studio.overlay;$project_root/firmware/zmk/boards/zuoya/gmk70_wired/standalone.overlay")
fi
if [[ "$variant" == wired-studio-native-bridge ]]; then
    extra+=(-DEXTRA_CONF_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/studio.conf;$project_root/firmware/zmk/boards/zuoya/gmk70_wired/native-update-bridge.conf")
fi
if [[ "$variant" == wired-studio-update-test ]]; then
    extra+=(-DEXTRA_CONF_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/studio.conf;$project_root/firmware/zmk/boards/zuoya/gmk70_wired/studio-update-test.conf")
fi
if [[ "$variant" == wired-studio-radio-qualification ]]; then
    extra+=(-DEXTRA_CONF_FILE="$project_root/firmware/zmk/boards/zuoya/gmk70_wired/studio.conf;$project_root/firmware/zmk/boards/zuoya/gmk70_wired/studio-radio-qualification.conf")
fi
python3 firmware/zmk/tools/workspace.py verify
zmk_tools="$project_root/firmware/zmk/tools/.venv/bin"
modules="$project_root/dependencies/modules/hal/cmsis;$project_root/dependencies/modules/lib/gui/lvgl"
if [[ "$variant" == wired-studio-* ]]; then
    modules+=";$project_root/dependencies/modules/lib/nanopb;$project_root/dependencies/modules/msgs/zmk-studio-messages"
    extra+=(-DNANOPB_PROTOC_EXECUTABLE="$zmk_tools/python-grpc-tools-protoc")
fi
for tool in python cmake ninja; do
    [[ -x "$zmk_tools/$tool" ]] || { printf 'Missing pinned ZMK build tools; run make setup-zmk\n' >&2; exit 2; }
done
command -v arm-none-eabi-gcc >/dev/null || { printf 'Missing ARM toolchain; run make setup\n' >&2; exit 2; }
for path in .work .work/locks .work/zmk .work/zmk/$variant .work/zmk/$variant/$mode artifacts artifacts/zmk artifacts/zmk/$variant; do
    [[ ! -L "$path" ]] || { printf 'Refusing redirected ZMK output: %s\n' "$path" >&2; exit 2; }
done
mkdir -p .work/zmk/$variant/$mode artifacts/zmk/$variant .work/locks
exec 9>.work/locks/zmk-$variant.lock
flock 9
export PATH="$zmk_tools:$PATH"
export ZEPHYR_BASE="$project_root/dependencies/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE="$(dirname "$(command -v arm-none-eabi-gcc)")/arm-none-eabi-"
export PYTHONDONTWRITEBYTECODE=1
build_dir="$project_root/.work/zmk/$variant/$mode"
output="$project_root/artifacts/zmk/$variant"
user_cache="$project_root/.work/zmk/cache"
[[ "$board" != gmk70_wired ]] || user_cache="$build_dir/cache"
cmake -S dependencies/zmk/app -B "$build_dir" -G Ninja \
    -DBOARD="$board" \
    -DZephyr_DIR="$ZEPHYR_BASE/share/zephyr-package/cmake" \
    -DZEPHYR_MODULES="$modules" \
    -DZMK_EXTRA_MODULES="$project_root/firmware/zmk" \
    -DPYTHON_EXECUTABLE="$zmk_tools/python" \
    -DUSER_CACHE_DIR="$user_cache" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCONFIG_BUILD_OUTPUT_HEX=n \
    "${extra[@]}" \
    2>&1 | tee "$output/configure.log"
cmake --build "$build_dir" --parallel "${JOBS:-8}" 2>&1 | tee "$output/build.log"
for extension in bin elf map; do
    cp "$build_dir/zephyr/zmk.$extension" "$output/zmk.$extension"
done
rm -f "$build_dir/zephyr/zmk.hex" "$output/zmk.hex" "$output/install.hex" "$output/from-install-hex.bin"
cp "$build_dir/zephyr/.config" "$output/zmk.config"
cp "$build_dir/compile_commands.json" "$output/"
cp "$build_dir/zephyr/zephyr.dts" "$output/"
cp dependencies/sources.lock.json "$output/"
if [[ "$board" == gmk70_wired ]]; then
    python3 firmware/zmk/tools/check_image.py "$output/zmk.elf" "$output/zmk.bin" --board "$board" --variant "$variant" > "$output/image-check.json"
    if [[ "$variant" == wired-studio-standalone ]]; then
        # Direct-boot binary only: never label it as a resident-compatible update.
        rm -f "$output/application.bin" "$output/resident.bin" "$output/install.bin" "$output/update.json"
    elif [[ "$variant" == wired-qualification ]]; then
        # Qualification can only enter through target-specific trial ABI2.
        # Never emit a normal application manifest for a staging-linked image.
        for target in left right; do
            python3 tools/firmware-update/trial.py package "$output/zmk.bin" --target "$target" --output "$output/$target"
        done
    else
        resident="$project_root/artifacts/qmk/debug-updater/resident.bin"
        [[ -f "$resident" ]] || { printf 'Missing reviewed pinned resident prerequisite: %s (ZMK will not rebuild it)\n' "$resident" >&2; exit 2; }
        python3 tools/firmware-update/zmk_package.py --image "$output/zmk.bin" --resident "$resident" \
            --report "$output/image-check.json" --output "$output"
    fi
else
    python3 firmware/zmk/tools/check_image.py "$output/zmk.elf" "$output/zmk.bin" --board "$board" > "$output/image-check.json"
fi
{
    if [[ "$board" == gmk70_wired ]]; then
        if [[ "$variant" == wired-studio-standalone ]]; then
            printf 'Standalone universal ZMK Studio; no resident installer or upload receiver.\n'
            printf 'zmk.bin: direct factory DFU at 0x08000000 on each half.\n'
            printf 'Existing ZMK settings banks remain at 0x0801c000..0x0801ffff.\n'
        elif [[ "$variant" == wired-studio-updater ]]; then
            printf 'ZMK Studio universal distribution; explicitly unlocked updater.\n'
            printf 'install.bin: full 256 KiB first install at 0x08000000 on each half; clears ZMK settings.\n'
        else
            printf 'EXPERIMENTAL UNLOCKED DEVELOPMENT: actual ZMK %s; never a normal release asset.\n' "$variant"
        fi
        printf 'Universal board: gmk70_wired; role B9; wired C14/C15; 10x9 combined matrix.\n'
        if [[ "$variant" == wired-qualification ]]; then
            printf 'Staging qualification at 0x08020000; no watchdog feed, running-code writes or durable install.\n'
            printf 'Only target-specific left/right TrialPackage manifests are emitted.\n'
        else
            if [[ "$variant" != wired-studio-standalone ]]; then
                printf 'Application at 0x08002000; native updater; no permanent watchdog or outgoing handoff.\n'
                printf 'Hardware reset activates updates; a hung application may require a physical power cycle.\n'
            fi
            if [[ "$variant" == wired-studio-* ]]; then
                printf 'Identical Studio application for LEFT and RIGHT.\n'
                printf 'LEFT owns CDC Studio settings; RIGHT never mounts/writes central settings banks.\n'
                printf 'Battery: stock-radio inquiry/ACK cache, native sensor and held RGB status.\n'
                printf 'Radio: stock vendor UART3 Bluetooth/dongle keyboard and consumer endpoint; USB Studio retained.\n'
                if [[ "$variant" == wired-studio-radio-qualification ]]; then
                    printf 'TEMPORARY QUALIFICATION: fixed F24 marker command; excluded from canonical firmware.\n'
                fi
            else
                printf 'profile=zmk package; exact pinned resident prerequisite preserved.\n'
            fi
        fi
        printf 'Four Windows/Mac layers; native local RGB and wired peer lighting.\n'
        printf 'Knob CCW/CW are two appended editable key positions; press remains unchanged.\n'
        printf 'No native BLE, wireless Studio, external Vial EEPROM or device operations.\n'
    else
        printf 'EXPERIMENTAL actual ZMK %s staging trial; not a normal updater image.\n' "$variant"
        printf 'Source: dependencies/zmk/app\nBoard: %s\nFlash: 0x08020000, maximum 120 KiB\n' "$board"
        printf 'RAM admitted: 0x20000040..0x20006FC0; upper retained record excluded.\n'
        printf 'The launcher watchdog is not fed; successful trial must return by reset.\n'
        printf 'No radio, EEPROM, split transport, flash writes, or device operations.\n'
    fi
    arm-none-eabi-gcc --version
    arm-none-eabi-size "$output/zmk.elf"
} > "$output/build-info.txt"
(cd "$output" && sha256sum zmk.bin zmk.elf zmk.map zmk.config zephyr.dts compile_commands.json sources.lock.json image-check.json build-info.txt > SHA256SUMS)
if [[ "$board" == gmk70_wired ]]; then
    if [[ "$variant" == wired-studio-standalone ]]; then
        cat > "$output/README.txt" <<'EOF'
GMK70 standalone ZMK Studio (same image for both halves)

Flash zmk.bin at 0x08000000 through each half's factory WB32 USB DFU
bootloader. This replaces any resident installer; left-USB split updating
is unavailable afterward. Do not use update.py or the application updater.

The image stops before the existing ZMK settings banks at 0x0801c000.
Preserving settings requires a flash-range write without a full-chip erase.
Back up Studio settings before migration. The bootloader key/startup chord
uses the standalone ROM-entry hook. Initial hardware validation is pending.

Typing, Studio over USB, saved settings, knob assignments, lighting,
Bluetooth/2.4 GHz and coordinated sleep use the same board implementation.
The shared service worker remains for those functions. No hardware watchdog.
EOF
        (cd "$output" && sha256sum zmk.bin README.txt image-check.json sources.lock.json build-info.txt > FIRMWARE-SHA256SUMS && python3 -m zipfile -c gmk70-zmk-standalone.zip zmk.bin README.txt image-check.json sources.lock.json build-info.txt FIRMWARE-SHA256SUMS)
    elif [[ "$variant" == wired-qualification ]]; then
        (cd "$output" && sha256sum left/trial.bin left/trial.json right/trial.bin right/trial.json >> SHA256SUMS)
    else
        (cd "$output" && sha256sum application.bin resident.bin update.json package-info.json >> SHA256SUMS)
        if [[ "$variant" == wired-studio-updater ]]; then
            (cd "$output" && sha256sum install.bin >> SHA256SUMS)
        fi
    fi
fi
printf '\nBuilt ZMK %s in %s\n' "$variant" "$output"

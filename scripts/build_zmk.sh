#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
variant="${1:-left-trial}"
[[ "$variant" == left-trial || "$variant" == right-trial ]] || { printf 'Unsupported experimental ZMK target: %s\n' "$variant" >&2; exit 2; }
board="gmk70_${variant%-trial}"
python3 firmware/zmk/tools/workspace.py verify
zmk_tools="$project_root/firmware/zmk/tools/.venv/bin"
for tool in python cmake ninja; do
    [[ -x "$zmk_tools/$tool" ]] || { printf 'Missing pinned ZMK build tools; run make setup-zmk\n' >&2; exit 2; }
done
command -v arm-none-eabi-gcc >/dev/null || { printf 'Missing ARM toolchain; run make setup\n' >&2; exit 2; }
for path in .work .work/locks .work/zmk .work/zmk/$variant .work/zmk/$variant/normal artifacts artifacts/zmk artifacts/zmk/$variant; do
    [[ ! -L "$path" ]] || { printf 'Refusing redirected ZMK output: %s\n' "$path" >&2; exit 2; }
done
mkdir -p .work/zmk/$variant/normal artifacts/zmk/$variant .work/locks
exec 9>.work/locks/zmk-$variant.lock
flock 9
export PATH="$zmk_tools:$PATH"
export ZEPHYR_BASE="$project_root/dependencies/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export CROSS_COMPILE="$(dirname "$(command -v arm-none-eabi-gcc)")/arm-none-eabi-"
export PYTHONDONTWRITEBYTECODE=1
build_dir="$project_root/.work/zmk/$variant/normal"
output="$project_root/artifacts/zmk/$variant"
cmake -S dependencies/zmk/app -B "$build_dir" -G Ninja \
    -DBOARD="$board" \
    -DZephyr_DIR="$ZEPHYR_BASE/share/zephyr-package/cmake" \
    -DZEPHYR_MODULES="$project_root/dependencies/modules/hal/cmsis;$project_root/dependencies/modules/lib/gui/lvgl" \
    -DZMK_EXTRA_MODULES="$project_root/firmware/zmk" \
    -DPYTHON_EXECUTABLE="$zmk_tools/python" \
    -DUSER_CACHE_DIR="$project_root/.work/zmk/cache" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    2>&1 | tee "$output/configure.log"
cmake --build "$build_dir" --parallel "${JOBS:-8}" 2>&1 | tee "$output/build.log"
for extension in bin hex elf map; do
    cp "$build_dir/zephyr/zmk.$extension" "$output/zmk.$extension"
done
cp "$build_dir/zephyr/.config" "$output/zmk.config"
cp "$build_dir/compile_commands.json" "$output/"
cp "$build_dir/zephyr/zephyr.dts" "$output/"
cp dependencies/sources.lock.json "$output/"
python3 firmware/zmk/tools/check_image.py "$output/zmk.elf" "$output/zmk.bin" --board "$board" > "$output/image-check.json"
{
    printf 'EXPERIMENTAL actual ZMK %s staging trial; not a normal updater image.\n' "$variant"
    printf 'Source: dependencies/zmk/app\nBoard: %s\nFlash: 0x08020000, maximum 120 KiB\n' "$board"
    printf 'RAM admitted: 0x20000040..0x20006FC0; upper retained record excluded.\n'
    printf 'The launcher watchdog is not fed; successful trial must return by reset.\n'
    printf 'No radio, EEPROM, split transport, flash writes, or device operations.\n'
    arm-none-eabi-gcc --version
    arm-none-eabi-size "$output/zmk.elf"
} > "$output/build-info.txt"
(cd "$output" && sha256sum zmk.bin zmk.hex zmk.elf zmk.map zmk.config zephyr.dts compile_commands.json sources.lock.json image-check.json build-info.txt > SHA256SUMS)
printf '\nBuilt experimental actual ZMK in %s\n' "$output"

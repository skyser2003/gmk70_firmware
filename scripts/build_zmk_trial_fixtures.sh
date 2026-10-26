#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
command -v arm-none-eabi-gcc >/dev/null || { printf 'Run make setup first\n' >&2; exit 2; }
work=.work/zmk/trial-fixtures
output=artifacts/zmk/trial-fixtures
[[ ! -L .work && ! -L .work/zmk && ! -L "$work" ]] || exit 2
mkdir -p "$work" "$output"
for fixture in return hang; do
    flags=()
    if [[ "$fixture" == return ]]; then flags+=(-DGMK_TRIAL_RETURN); fi
    arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -nostdlib -Wl,--build-id=none \
        -T firmware/zmk/trial/fixture.ld -Wl,-Map="$work/$fixture.map" \
        "${flags[@]}" firmware/zmk/trial/fixture.S -o "$work/$fixture.elf"
    arm-none-eabi-objcopy -O binary "$work/$fixture.elf" "$work/$fixture.bin"
    for half in left right; do
        uv run --offline --frozen --project tools/firmware-update python \
            tools/firmware-update/trial.py package "$work/$fixture.bin" \
            --target "$half" --output "$output/$fixture-$half"
        cp "$work/$fixture.elf" "$work/$fixture.map" "$output/$fixture-$half/"
        cp docs/ZMK-TRIAL-LAUNCHER.md "$output/$fixture-$half/README.md"
        cp tools/firmware-update/trial.py "$output/$fixture-$half/"
        (
            cd "$output/$fixture-$half"
            sha256sum trial.bin trial.json "$fixture.elf" "$fixture.map" README.md trial.py > SHA256SUMS
        )
    done
done
printf 'Built OFFLINE trial fixtures in %s/%s; no launch or device operation performed.\n' "$project_root" "$output"

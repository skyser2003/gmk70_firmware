#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Compilation only. Never opens, resets, reads, or writes a physical device.
set -euo pipefail
if [[ $# != 2 || ! $1 =~ ^0x[0-9a-fA-F]{8}$ || $2 != 2048 ]]; then
    printf 'Usage: bash %s 0xXXXXXXXX 2048\nAddress must come from a separately verified allocator return.\n' "$0" >&2
    exit 2
fi
trial_base=$(( $1 ))
if (( trial_base < 0x11007b18 || trial_base % 4 != 0 || trial_base + 2048 > 0x1100e000 )); then
    printf 'Allocation is outside the supported aligned RAM range.\n' >&2
    exit 2
fi
trial_source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
trial_repo_root="$(cd "$trial_source_dir/../../../.." && pwd -P)"
source "$trial_repo_root/scripts/project.sh"
trial_output="$trial_repo_root/.work/radio/ram-vial-trial/normal"
mkdir -p "$trial_output"
printf -v trial_end '0x%08x' "$(( trial_base + 2048 ))"
printf -v trial_status '0x%08x' "$(( trial_base + 2048 - 16 ))"
trial_flags=(-mcpu=cortex-m3 -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin
    -fshort-enums -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
# Both code and data reside in the same deliberately owned SRAM block.
trial_link_flags=(-mcpu=cortex-m3 -mthumb -nostdlib -Wl,--no-warn-rwx-segments)
arm-none-eabi-gcc "${trial_flags[@]}" -I"$trial_source_dir" -c "$trial_source_dir/payload.c" -o "$trial_output/payload.o"
arm-none-eabi-gcc -E -P -x c -DTRIAL_ALLOCATED_BASE="$1" -DTRIAL_ALLOCATED_END="$trial_end" \
    "$trial_source_dir/payload.ld.in" -o "$trial_output/payload.ld"
arm-none-eabi-gcc "${trial_link_flags[@]}" -Wl,-T,"$trial_output/payload.ld" \
    "$trial_output/payload.o" -o "$trial_output/hid-service-NOT-INSTALLED.elf"
arm-none-eabi-objcopy -O binary --set-section-flags .bss=alloc,load,contents \
    "$trial_output/hid-service-NOT-INSTALLED.elf" "$trial_output/hid-service-NOT-INSTALLED.bin"
arm-none-eabi-nm -n "$trial_output/hid-service-NOT-INSTALLED.elf" > "$trial_output/payload.sym"
trial_symbol() {
    awk -v symbol="$1" '$3 == symbol { print "0x" $1; exit }' "$trial_output/payload.sym"
}
trial_payload_end="$(trial_symbol __payload_end)"
arm-none-eabi-gcc "${trial_flags[@]}" -DGMK70_TRIAL_STATUS_ADDRESS="$trial_status" \
    -c "$trial_source_dir/reconnect.c" -o "$trial_output/reconnect.o"
arm-none-eabi-gcc -E -P -x c -DTRIAL_PAYLOAD_END="$trial_payload_end" -DTRIAL_ALLOCATED_END="$trial_end" \
    "$trial_source_dir/reconnect.ld.in" -o "$trial_output/reconnect.ld"
arm-none-eabi-gcc "${trial_link_flags[@]}" -Wl,-T,"$trial_output/reconnect.ld" \
    "$trial_output/reconnect.o" -o "$trial_output/reconnect.elf"
arm-none-eabi-objcopy -O binary "$trial_output/reconnect.elf" "$trial_output/reconnect.bin"
for trial_kind in install route refresh reconnect; do
    trial_oneshot=1
    case "$trial_kind" in
        install) trial_target="$(trial_symbol radio_install)" ;;
        route) trial_target="$(trial_symbol radio_reply)"; trial_oneshot=0 ;;
        refresh) trial_target="$(trial_symbol radio_refresh)" ;;
        reconnect) trial_target="$trial_payload_end" ;;
    esac
    printf -v trial_target '0x%08x' "$(( trial_target | 1 ))"
    arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -DONE_SHOT="$trial_oneshot" \
        -c "$trial_source_dir/gateway.S" -o "$trial_output/$trial_kind-gateway.o"
    arm-none-eabi-gcc "${trial_link_flags[@]}" -Wl,-T,"$trial_source_dir/gateway.ld" \
        -Wl,--defsym,__trial_target="$trial_target" "$trial_output/$trial_kind-gateway.o" \
        -o "$trial_output/$trial_kind-gateway.elf"
    arm-none-eabi-objcopy -O binary "$trial_output/$trial_kind-gateway.elf" "$trial_output/$trial_kind-gateway.bin"
done
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -c "$trial_source_dir/allocation.S" -o "$trial_output/allocation.o"
arm-none-eabi-gcc "${trial_link_flags[@]}" -Wl,-T,"$trial_source_dir/allocation.ld" \
    "$trial_output/allocation.o" -o "$trial_output/allocation-UNCONFIGURED-NOT-INSTALLABLE.elf"
arm-none-eabi-objcopy -O binary "$trial_output/allocation-UNCONFIGURED-NOT-INSTALLABLE.elf" \
    "$trial_output/allocation-UNCONFIGURED-NOT-INSTALLABLE.bin"
sha256sum "$trial_output"/*.bin > "$trial_output/SHA256SUMS"
printf 'Offline RAM trial binaries built under %s\nNo device operation was performed.\n' "$trial_output"

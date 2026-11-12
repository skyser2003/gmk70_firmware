#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p .work/zmk/tests
uv run --no-sync --project tools/build-support python tests/zmk/recovery_native.py \
    .work/zmk/tests/native_recovery_reset.c
flags=(-std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie
    -fsanitize=address,undefined -fno-omit-frame-pointer
    -Itests/zmk/recovery_stubs -Idependencies/zmk/app/include)
cc "${flags[@]}" -Wno-unused-parameter -c .work/zmk/tests/native_recovery_reset.c \
    -o .work/zmk/tests/native_recovery_reset.o
sources=(tests/zmk/recovery_test.c firmware/zmk/recovery/core.c
    .work/zmk/tests/native_recovery_reset.o)
cc "${flags[@]}" -no-pie "${sources[@]}" -Wl,--wrap=sys_reboot \
    -o .work/zmk/tests/recovery_test
.work/zmk/tests/recovery_test
cc -Itests/zmk/recovery_startup_stubs "${flags[@]}" -no-pie tests/zmk/recovery_startup_test.c \
    firmware/zmk/recovery/core.c \
    -Ifirmware/zmk/wired -Wl,--wrap=sys_reboot -Wl,--wrap=gmk_recovery_request -ffunction-sections -Wl,--gc-sections \
    -o .work/zmk/tests/recovery-startup
.work/zmk/tests/recovery-startup
for split in 0 1; do
    for metadata in 0 1; do
        cc -Itests/zmk/recovery_stubs -Itests/zmk/radio_behavior_stubs "${flags[@]}" \
            -no-pie -DCONFIG_ZMK_SPLIT="$split" -DCONFIG_ZMK_BEHAVIOR_METADATA="$metadata" \
            tests/zmk/recovery_behavior_test.c -o ".work/zmk/tests/recovery-behavior-$split-$metadata"
        ".work/zmk/tests/recovery-behavior-$split-$metadata"
    done
done

bash tests/zmk/run_recovery_settings_tests.sh

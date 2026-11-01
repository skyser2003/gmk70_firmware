#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk .work/zmk/pixels-native/normal/include/zephyr/sys
cat >.work/zmk/pixels-native/normal/include/zephyr/sys/barrier.h <<'STUB'
/* Native barrier observation hook; production uses Zephyr's ARM DMB. */
#pragma once
void barrier_dmem_fence_full(void);
STUB
cc -std=c11 -Wall -Wextra -Werror -fno-pie -no-pie \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I.work/zmk/pixels-native/normal/include -Itests/zmk/wired_stubs -Itests/zmk/clock_stubs \
    tests/zmk/pixels_test.c -o artifacts/tests/zmk/pixels
artifacts/tests/zmk/pixels
cc -std=c11 -Wall -Wextra -Werror -fno-pie -no-pie \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -DTEST_PIXELS_LIGHTING=1 -DCONFIG_GMK70_WB32_RGB=1 -DCONFIG_GMK70_BATTERY=1 \
    -I.work/zmk/pixels-native/normal/include -Itests/zmk/lighting_stubs \
    -Itests/zmk/wired_stubs -Itests/zmk/clock_stubs \
    -Ifirmware/zmk/lighting -Ifirmware/zmk/wired -Ifirmware/zmk/battery \
    tests/zmk/pixels_test.c firmware/zmk/lighting/lighting_frame.c \
    firmware/zmk/lighting/battery_view.c firmware/qmk-common/keyboards/zuoya/gmk70/battery_holds.c \
    firmware/zmk/wired/wired_protocol.c -Wl,--wrap=gmk_protocol_lighting \
    -o artifacts/tests/zmk/pixels-lighting
artifacts/tests/zmk/pixels-lighting

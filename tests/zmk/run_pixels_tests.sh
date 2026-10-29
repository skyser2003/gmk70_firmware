#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk .work/zmk/pixels-native/normal/include/zephyr/sys
cat > .work/zmk/pixels-native/normal/include/zephyr/sys/barrier.h <<'STUB'
/* Native barrier observation hook; production uses Zephyr's ARM DMB. */
#pragma once
void barrier_dmem_fence_full(void);
STUB
cc -std=c11 -Wall -Wextra -Werror -fno-pie -no-pie \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I.work/zmk/pixels-native/normal/include -Itests/zmk/wired_stubs -Itests/zmk/clock_stubs \
    tests/zmk/pixels_test.c -o artifacts/tests/zmk/pixels
artifacts/tests/zmk/pixels

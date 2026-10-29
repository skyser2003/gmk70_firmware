#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-pie -no-pie -ffunction-sections -Wl,--gc-sections \
    -Itests/zmk/handoff_stubs -Itests/zmk/clock_stubs -Ifirmware/zmk/soc/westberry/wb32fq95 \
    -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
    tests/zmk/resident_handoff_test.c firmware/zmk/update/resident_handoff.c \
    firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c -o artifacts/tests/zmk/resident-handoff
artifacts/tests/zmk/resident-handoff
if [[ "${GMK70_TEST_BUILT_ZMK_RESIDENT:-0}" == 1 ]]; then
    artifacts/tests/zmk/resident-handoff artifacts/qmk/debug-updater/resident.bin
fi

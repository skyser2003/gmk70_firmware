#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p .work/zmk/tests
firmware/zmk/tools/.venv/bin/python firmware/zmk/tools/wired_pins.py \
    --keyboard firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json \
    --output .work/zmk/wired-native/normal/gmk70_wired_pins.h
for radio in 0 1; do
    flags=()
    if [[ "$radio" == 1 ]]; then flags+=(-DCONFIG_GMK70_RADIO=1); fi
    cc -DCONFIG_GMK70_NATIVE_UPDATE_LEGACY_BRIDGE=1 -std=c11 -g -Wall -Wextra -Werror \
        -fsanitize=undefined -fno-sanitize-recover=all "${flags[@]}" \
        -Itests/zmk/native_encoder_stubs -Itests/zmk/wired_stubs -Itests/zmk/clock_stubs \
        -Itests/zmk/radio_native_stubs -Ifirmware/zmk/wired \
        -Ifirmware/zmk/soc/westberry/wb32fq95 -I.work/zmk/wired-native/normal \
        tests/zmk/wired_power_test.c \
        firmware/zmk/wired/wired_protocol.c firmware/zmk/wired/encoder.c \
        -o ".work/zmk/tests/wired-power-$radio"
    ".work/zmk/tests/wired-power-$radio"
done

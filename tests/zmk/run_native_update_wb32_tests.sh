#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
firmware/zmk/tools/.venv/bin/python firmware/zmk/tools/wired_pins.py \
    --keyboard firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json \
    --output .work/zmk/native-update-wb32/normal/gmk70_wired_pins.h
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -DCONFIG_GMK70_NATIVE_UPDATE_WIRE=1 \
    -Itests/zmk/native_update_stubs -Itests/zmk/native_encoder_stubs \
    -Itests/zmk/wired_stubs -Itests/zmk/clock_stubs -Itests/zmk/radio_native_stubs \
    -Ifirmware/zmk/wired -Ifirmware/zmk/soc/westberry/wb32fq95 \
    -I.work/zmk/native-update-wb32/normal -Idependencies/zmk/app/include \
    -Idependencies/zephyr/include -Idependencies/zmk/app/src/split \
    tests/zmk/wired_wb32_test.c firmware/zmk/wired/wired_protocol.c \
    firmware/zmk/wired/encoder.c firmware/zmk/wired/native_update_wire.c \
    dependencies/zmk/app/src/split/wired/wired.c dependencies/zephyr/lib/utils/ring_buffer.c \
    dependencies/zephyr/lib/crc/crc32_sw.c -o artifacts/tests/zmk/wired-native-updater
artifacts/tests/zmk/wired-native-updater

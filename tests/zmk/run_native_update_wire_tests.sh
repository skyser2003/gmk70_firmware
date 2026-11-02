#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Itests/zmk/native_update_stubs -Itests/zmk/native_encoder_stubs \
    -Itests/zmk/wired_stubs -Itests/zmk/radio_native_stubs \
    -Idependencies/zmk/app/include -Idependencies/zephyr/include \
    -Idependencies/zmk/app/src/split -Ifirmware/zmk/wired \
    tests/zmk/native_update_wire_test.c firmware/zmk/wired/native_update_wire.c \
    dependencies/zmk/app/src/split/wired/wired.c dependencies/zephyr/lib/utils/ring_buffer.c \
    dependencies/zephyr/lib/crc/crc32_sw.c -o artifacts/tests/zmk/native-update-wire
artifacts/tests/zmk/native-update-wire
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Itests/zmk/native_update_stubs -Itests/zmk/native_encoder_stubs \
    -Itests/zmk/wired_stubs -Itests/zmk/radio_native_stubs \
    -Idependencies/zmk/app/include -Idependencies/zephyr/include \
    -Idependencies/zmk/app/src/split -Ifirmware/zmk/wired \
    -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
    tests/zmk/native_update_bridge_test.c firmware/zmk/wired/native_update_bridge.c \
    firmware/zmk/wired/native_update_wire.c dependencies/zmk/app/src/split/wired/wired.c \
    dependencies/zephyr/lib/utils/ring_buffer.c dependencies/zephyr/lib/crc/crc32_sw.c \
    -o artifacts/tests/zmk/native-update-bridge
artifacts/tests/zmk/native-update-bridge

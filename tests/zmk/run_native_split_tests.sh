#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
cc_args=(-std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all
    -DCONFIG_ZMK_SPLIT=1 -DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=1 -DCONFIG_GMK70_NATIVE_UPDATE_WIRE=1 -DCONFIG_GMK70_POWER=1
    -Itests/zmk/native_split_stubs -Itests/zmk/wired_stubs
    -Itests/zmk/native_encoder_stubs -Idependencies/zmk/app/include
    -Itests/zmk/radio_native_stubs -Ifirmware/zmk/wired
    -include zephyr/sys/util.h -include zephyr/init.h)
cc "${cc_args[@]}" -Wno-error=unused-parameter -Dactive_transport=gmk_native_peripheral_transport \
    -c dependencies/zmk/app/src/split/peripheral.c -o artifacts/tests/zmk/native-peripheral.o
cc "${cc_args[@]}" tests/zmk/native_split_test.c tests/zmk/native_split_events.c dependencies/zmk/app/src/stdlib.c \
    firmware/zmk/wired/wired_protocol.c \
    artifacts/tests/zmk/native-peripheral.o -Wl,--wrap=raise_zmk_position_state_changed \
    -o artifacts/tests/zmk/native-split
artifacts/tests/zmk/native-split

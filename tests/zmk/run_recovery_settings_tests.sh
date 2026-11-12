#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
flags=(-std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -fsanitize=address,undefined
    -fno-sanitize-recover=all -ffunction-sections -fdata-sections
    -include tests/zmk/recovery_settings_prelude.h -DTEST_FACTORY_RESET=1 -DCONFIG_ZMK_KEYMAP_SETTINGS_STORAGE=1
    -DCONFIG_ZMK_KEYMAP_LAYER_REORDERING=1 -DCONFIG_ZMK_STUDIO=1 -DCONFIG_ZMK_SPLIT=1
    -DCONFIG_ZMK_BEHAVIOR_METADATA=0 -DCONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS=1
    -DCONFIG_ZMK_KEYMAP_LAYER_NAME_MAX_LEN=16
    -Itests/zmk/radio_native_stubs -Itests/zmk/radio_kernel_stubs
    -Itests/zmk/radio_behavior_stubs -Itests/zmk/radio_endpoint_stubs
    -Idependencies/zmk/app/include -Ifirmware/zmk/update
    -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update)
cc "${flags[@]}" -O2 -Wno-unused-parameter -DRADIO_NATIVE_SOURCE=1 \
    -c dependencies/zmk/app/src/keymap.c -o .work/zmk/tests/recovery-native-keymap.o
cc "${flags[@]}" -no-pie -Wl,--gc-sections tests/zmk/recovery_settings_test.c \
    firmware/zmk/recovery/settings.c .work/zmk/tests/recovery-native-keymap.o \
    -o .work/zmk/tests/recovery-settings
.work/zmk/tests/recovery-settings

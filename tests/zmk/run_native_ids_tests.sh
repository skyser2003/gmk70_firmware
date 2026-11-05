#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
flags=(-std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -fsanitize=address,undefined
    -ffunction-sections -fdata-sections -DCONFIG_ZMK_BEHAVIOR_LOCAL_IDS=1
    -DCONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE=1 -DCONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_CRC16=0
    -DCONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS=1 -DCONFIG_ZMK_BEHAVIOR_METADATA=0
    -DCONFIG_ZMK_SPLIT=0 -DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=0 -DCONFIG_LOG=0
    -include tests/zmk/native_ids_stubs/prelude.h -Itests/zmk/native_ids_stubs
    -Itests/zmk/studio_stubs -Itests/zmk/radio_native_stubs -Itests/zmk/radio_behavior_stubs
    -Itests/zmk/radio_endpoint_stubs -Idependencies/zmk/app/include)
cc "${flags[@]}" -Wno-unused-parameter -c dependencies/zmk/app/src/behavior.c -o .work/zmk/tests/behavior-native-ids.o
cc "${flags[@]}" -no-pie -Wl,--gc-sections tests/zmk/native_ids_test.c .work/zmk/tests/behavior-native-ids.o -o .work/zmk/tests/native-ids
for scenario in baseline reserved zero-seed behavior-subtree keymap-subtree direct headroom invalid; do
    .work/zmk/tests/native-ids "$scenario"
done

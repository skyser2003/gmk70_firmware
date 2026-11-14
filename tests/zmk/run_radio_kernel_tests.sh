#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
for profile in normal normal-event qualification nkro nkro-qualification; do
    flags=()
    if [[ "$profile" == normal-event ]]; then flags+=(-DCONFIG_GMK70_EVENT_IDLE=1); fi
    if [[ "$profile" == *qualification ]]; then flags+=(-DCONFIG_GMK70_RADIO_QUALIFICATION=1); fi
    if [[ "$profile" == nkro* ]]; then flags+=(-DCONFIG_ZMK_HID_REPORT_TYPE_NKRO=1); fi
    cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined -ffunction-sections -Wl,--gc-sections \
        "${flags[@]}" -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1 -DCONFIG_GMK70_POWER=1 \
        -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
        -Itests/zmk/radio_kernel_stubs -Itests/zmk/radio_behavior_stubs \
        -Itests/zmk/radio_endpoint_stubs \
        tests/zmk/radio_kernel_test.c firmware/zmk/radio/core.c firmware/zmk/radio/selection.c \
        -o ".work/zmk/tests/radio-kernel-$profile"
    ".work/zmk/tests/radio-kernel-$profile"
done

# Compile the pinned native implementation, keeping GNU wrapping across real translation units.
native_flags=(-DCONFIG_GMK70_NATIVE_KEYMAP_TEST=1 -DCONFIG_ZMK_KEYMAP_SETTINGS_STORAGE=1 -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1
    -DCONFIG_ZMK_KEYMAP_LAYER_REORDERING=1 -DCONFIG_ZMK_STUDIO=1 -DCONFIG_ZMK_SPLIT=1
    -DCONFIG_ZMK_BEHAVIOR_METADATA=0 -DCONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS=1 -DCONFIG_ZMK_KEYMAP_LAYER_NAME_MAX_LEN=16)
native_includes=(-Itests/zmk/radio_native_stubs
    -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update
    -Itests/zmk/radio_kernel_stubs -Itests/zmk/radio_behavior_stubs -Itests/zmk/radio_endpoint_stubs -Idependencies/zmk/app/include)
native_warnings=(-std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -fsanitize=address,undefined
    -fno-sanitize-recover=all -ffunction-sections -fdata-sections)
# Pinned upstream has intentionally unused platform/callback parameters; do not edit the dependency.
cc "${native_warnings[@]}" -O2 -Wno-unused-parameter "${native_flags[@]}" "${native_includes[@]}" \
    -DRADIO_NATIVE_SOURCE=1 -c dependencies/zmk/app/src/keymap.c -o .work/zmk/tests/radio-native-keymap.o
mapfile -t native_wraps < <(sed -n 's/.*zephyr_ld_options(\(-Wl,--wrap=zmk_keymap_[a-z_]*\)).*/\1/p' firmware/zmk/radio/CMakeLists.txt)
for profile in normal qualification nkro nkro-qualification; do
    flags=()
    if [[ "$profile" == *qualification ]]; then flags+=(-DCONFIG_GMK70_RADIO_QUALIFICATION=1); fi
    if [[ "$profile" == nkro* ]]; then flags+=(-DCONFIG_ZMK_HID_REPORT_TYPE_NKRO=1); fi
    cc "${native_warnings[@]}" -no-pie -Wl,--gc-sections "${native_wraps[@]}" \
        "${native_flags[@]}" "${flags[@]}" "${native_includes[@]}" \
        tests/zmk/radio_kernel_test.c firmware/zmk/radio/core.c firmware/zmk/radio/selection.c \
        .work/zmk/tests/radio-native-keymap.o -o ".work/zmk/tests/radio-native-keymap-$profile"
    ".work/zmk/tests/radio-native-keymap-$profile"
done

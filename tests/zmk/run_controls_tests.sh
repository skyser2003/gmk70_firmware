#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
flags=(-std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -fsanitize=address,undefined
    -DCONFIG_GMK70_CONTROLS=1 -DCONFIG_GMK70_RADIO=1
    -DCONFIG_ZMK_HID_REPORT_TYPE_NKRO=1 -DCONFIG_ZMK_HID_REPORT_TYPE_HKRO=0
    -DCONFIG_ZMK_USB_BOOT=1 -DCONFIG_ZMK_POINTING=0 -DCONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6
    -DCONFIG_ZMK_HID_SEPARATE_MOD_RELEASE_REPORT=0 -DCONFIG_ZMK_SPLIT=0
    -DCONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS=0
    -include tests/zmk/controls_native_prelude.h -Itests/zmk/controls_stubs
    -Itests/zmk/radio_kernel_stubs -Itests/zmk/function_flip_stubs
    -Idependencies/zmk/app/include -Ifirmware/zmk/update
    -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update)
# Keep the pinned upstream signed/sizeof warning isolated from board code.
cc "${flags[@]}" -Wno-sign-compare -c dependencies/zmk/app/src/hid.c -o .work/zmk/tests/controls-native-hid.o
cc "${flags[@]}" -Wno-unused-parameter -c tests/zmk/controls_native_indicators.c -o .work/zmk/tests/controls-native-indicators.o
for backend in runtime studio; do
    backend_flags=()
    if [[ "$backend" == studio ]]; then backend_flags+=(-DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1); fi
    cc "${flags[@]}" "${backend_flags[@]}" -Wconversion -Wsign-conversion -fanalyzer -no-pie tests/zmk/controls_test.c \
        .work/zmk/tests/controls-native-hid.o .work/zmk/tests/controls-native-indicators.o firmware/zmk/lighting/battery_view.c \
        firmware/qmk-common/keyboards/zuoya/gmk70/battery_holds.c \
        firmware/qmk-common/keyboards/zuoya/gmk70/stock_power.c -o ".work/zmk/tests/controls-$backend"
    ".work/zmk/tests/controls-$backend"
done

#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
flags=(-std=gnu11 -O1 -g -Wall -Wextra -Werror -pthread -fno-pie -fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -fdata-sections
    -DCONFIG_SETTINGS=1 -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1
    -Itests/zmk/rgb_native_stubs -Itests/zmk/studio_stubs -Idependencies/zmk/app/include
    -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update)
# Only pinned upstream callback parameters are exempt from the native strict warnings.
cc "${flags[@]}" -Wno-unused-parameter \
    -DCONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN=0 -DCONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX=100 \
    -DCONFIG_ZMK_RGB_UNDERGLOW_HUE_START=0 -DCONFIG_ZMK_RGB_UNDERGLOW_SAT_START=100 \
    -DCONFIG_ZMK_RGB_UNDERGLOW_BRT_START=20 -DCONFIG_ZMK_RGB_UNDERGLOW_SPD_START=1 \
    -DCONFIG_ZMK_RGB_UNDERGLOW_EFF_START=0 -DCONFIG_ZMK_RGB_UNDERGLOW_ON_START=1 \
    -DCONFIG_ZMK_RGB_UNDERGLOW_HUE_STEP=10 -DCONFIG_ZMK_RGB_UNDERGLOW_SAT_STEP=10 \
    -DCONFIG_ZMK_RGB_UNDERGLOW_BRT_STEP=10 -DCONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE=60000 \
    -c dependencies/zmk/app/src/rgb_underglow.c -o .work/zmk/tests/rgb-native.o
mapfile -t wraps < <(sed -n 's/.*zephyr_ld_options(\(-Wl,--wrap=[a-z_]*\)).*/\1/p' firmware/zmk/lighting/CMakeLists.txt)
cc "${flags[@]}" -no-pie -Wl,--gc-sections "${wraps[@]}" \
    tests/zmk/rgb_native_settings_test.c firmware/zmk/lighting/rgb_settings.c \
    firmware/zmk/studio/settings_backend.c firmware/zmk/studio/settings_store.c \
    firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c .work/zmk/tests/rgb-native.o \
    -o .work/zmk/tests/rgb-native-settings
.work/zmk/tests/rgb-native-settings "${@}"

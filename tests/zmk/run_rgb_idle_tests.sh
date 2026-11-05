#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
flags=(-std=gnu11 -O1 -g -Wall -Wextra -Werror -pthread -fno-pie -fsanitize=address,undefined
  -fno-sanitize-recover=all -ffunction-sections -fdata-sections
  -DCONFIG_SETTINGS=1 -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1 -DCONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE=1
  -include tests/zmk/rgb_idle_native_prelude.h -Itests/zmk/rgb_native_stubs
  -Itests/zmk/studio_stubs -Idependencies/zmk/app/include -Itests/zmk/function_flip_stubs
  -Itests/zmk/controls_stubs -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update)
cc "${flags[@]}" -Wno-unused-parameter \
  -DCONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN=0 -DCONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX=100 \
  -DCONFIG_ZMK_RGB_UNDERGLOW_HUE_START=0 -DCONFIG_ZMK_RGB_UNDERGLOW_SAT_START=100 \
  -DCONFIG_ZMK_RGB_UNDERGLOW_BRT_START=20 -DCONFIG_ZMK_RGB_UNDERGLOW_SPD_START=1 \
  -DCONFIG_ZMK_RGB_UNDERGLOW_EFF_START=0 -DCONFIG_ZMK_RGB_UNDERGLOW_ON_START=1 \
  -DCONFIG_ZMK_RGB_UNDERGLOW_HUE_STEP=10 -DCONFIG_ZMK_RGB_UNDERGLOW_SAT_STEP=10 \
  -DCONFIG_ZMK_RGB_UNDERGLOW_BRT_STEP=10 -DCONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE=60000 \
  -c tests/zmk/rgb_idle_native_source.c -o artifacts/tests/zmk/rgb-idle-native.o
cc "${flags[@]}" -Wconversion -Wsign-conversion -fanalyzer -finstrument-functions -c firmware/zmk/lighting/rgb_settings.c \
  -o artifacts/tests/zmk/rgb-idle-adapter.o
cc "${flags[@]}" -no-pie -Wl,--gc-sections tests/zmk/rgb_idle_native_test.c \
  dependencies/zmk/app/src/events/activity_state_changed.c artifacts/tests/zmk/rgb-idle-native.o \
  artifacts/tests/zmk/rgb-idle-adapter.o -Wl,--wrap=settings_save_one \
  -Wl,--wrap=zmk_rgb_underglow_set_hsb -Wl,--wrap=raise_zmk_activity_state_changed \
  -Wl,--wrap=zmk_rgb_underglow_on -Wl,--wrap=zmk_rgb_underglow_off \
  -Wl,--wrap=zmk_rgb_underglow_toggle -o artifacts/tests/zmk/rgb-idle-native
for scenario in on off idle-off idle-on idle-toggle sleep late-off; do
  artifacts/tests/zmk/rgb-idle-native "$scenario"
done

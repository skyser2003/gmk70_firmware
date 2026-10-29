#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
flags=(-std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
       -Ifirmware/zmk/lighting -Ifirmware/zmk/battery -Ifirmware/zmk/wired)
cc "${flags[@]}" tests/zmk/battery_view_test.c firmware/zmk/lighting/battery_view.c -o artifacts/tests/zmk/battery-view
artifacts/tests/zmk/battery-view
cc "${flags[@]}" -DCONFIG_GMK70_BATTERY=1 -Itests/zmk/lighting_stubs -Itests/zmk/wired_stubs \
  tests/zmk/lighting_battery_test.c firmware/zmk/lighting/battery_view.c firmware/zmk/lighting/lighting_frame.c \
  firmware/zmk/wired/wired_protocol.c -Wl,--wrap=gmk_protocol_lighting -o artifacts/tests/zmk/lighting-battery
artifacts/tests/zmk/lighting-battery
cc "${flags[@]}" -DCONFIG_GMK70_BATTERY=1 -DCONFIG_GMK70_RADIO=1 \
  -Itests/zmk/lighting_stubs -Itests/zmk/wired_stubs \
  tests/zmk/lighting_battery_test.c firmware/zmk/lighting/battery_view.c firmware/zmk/lighting/lighting_frame.c \
  firmware/zmk/wired/wired_protocol.c -Wl,--wrap=gmk_protocol_lighting -o artifacts/tests/zmk/lighting-battery-radio
artifacts/tests/zmk/lighting-battery-radio

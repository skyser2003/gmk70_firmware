#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Ifirmware/zmk/lighting -Ifirmware/zmk/wired \
  tests/zmk/lighting_frame_test.c firmware/zmk/lighting/lighting_frame.c \
  firmware/zmk/wired/wired_protocol.c -o artifacts/tests/zmk/lighting-frame
artifacts/tests/zmk/lighting-frame
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Itests/zmk/lighting_stubs -Itests/zmk/wired_stubs \
  -Ifirmware/zmk/lighting -Ifirmware/zmk/wired \
  tests/zmk/lighting_test.c firmware/zmk/lighting/lighting_frame.c \
  firmware/zmk/wired/wired_protocol.c -Wl,--wrap=gmk_protocol_lighting -o artifacts/tests/zmk/lighting
artifacts/tests/zmk/lighting
cc -std=c11 -Wall -Wextra -Werror -Idependencies/zmk/app/include \
  tests/zmk/rgb_settings_test.c firmware/zmk/lighting/rgb_settings.c \
  -o artifacts/tests/zmk/rgb-settings
artifacts/tests/zmk/rgb-settings

#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
for backend in runtime studio; do
flags=()
if [[ "$backend" == studio ]]; then flags+=(-DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1); fi
cc -std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    -DCONFIG_GMK70_RADIO=1 -DCONFIG_ZMK_HID_REPORT_TYPE_NKRO=1 -DCONFIG_GMK70_CONTROLS=1 "${flags[@]}" \
    -Itests/zmk/function_flip_stubs -Itests/zmk/radio_kernel_stubs -Itests/zmk/radio_endpoint_stubs \
    -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
    tests/zmk/nkro_test.c firmware/zmk/studio/bool_setting.c firmware/qmk-common/drivers/wireless/radio_keys.c \
    -o ".work/zmk/tests/nkro-$backend"
".work/zmk/tests/nkro-$backend"
done
for metadata in 0 1; do
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    -DCONFIG_ZMK_BEHAVIOR_METADATA="$metadata" -Itests/zmk/radio_behavior_stubs \
    tests/zmk/nkro_behavior_test.c -o ".work/zmk/tests/nkro-behavior-$metadata"
".work/zmk/tests/nkro-behavior-$metadata"
done

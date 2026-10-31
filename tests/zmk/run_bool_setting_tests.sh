#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1 \
    -Itests/zmk/function_flip_stubs -Itests/zmk/radio_kernel_stubs \
    -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
    tests/zmk/bool_setting_test.c firmware/zmk/studio/bool_setting.c -o .work/zmk/tests/bool-setting
.work/zmk/tests/bool-setting
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    -Itests/zmk/function_flip_stubs -Itests/zmk/radio_kernel_stubs \
    tests/zmk/bool_setting_no_backend_test.c firmware/zmk/studio/bool_setting.c -o .work/zmk/tests/bool-setting-no-backend
.work/zmk/tests/bool-setting-no-backend

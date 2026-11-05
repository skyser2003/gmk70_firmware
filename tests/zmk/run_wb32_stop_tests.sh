#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p .work/zmk/tests
sources=(tests/zmk/wb32_stop_test.c firmware/qmk-common/drivers/wireless/wb32_stop.c)
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie \
    -fsanitize=address,undefined -fno-omit-frame-pointer "${sources[@]}" \
    -o .work/zmk/tests/wb32_stop_test
.work/zmk/tests/wb32_stop_test

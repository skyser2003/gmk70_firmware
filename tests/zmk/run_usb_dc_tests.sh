#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
uv run --no-sync --project tools/build-support python tests/zmk/usb_wake_native.py \
    .work/zmk/tests/native_usb_wakeup.h
cc -std=c11 -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
    -include .work/zmk/tests/native_usb_wakeup.h \
    -Itests/zmk/stubs -Idependencies/zephyr/include \
    tests/zmk/usb_dc_wb32_test.c \
    -o .work/zmk/tests/usb_dc_wb32_test
.work/zmk/tests/usb_dc_wb32_test
bash tests/zmk/run_usb_power_tests.sh

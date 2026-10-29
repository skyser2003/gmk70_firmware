#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
  tests/zmk/battery_status_test.c firmware/zmk/update/battery_status.c \
  firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c -o artifacts/tests/zmk/battery-status
artifacts/tests/zmk/battery-status

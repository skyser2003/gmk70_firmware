#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wsign-conversion -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  -DCONFIG_GMK70_BATTERY=1 -DCONFIG_GMK70_RADIO=1 \
  -Itests/zmk/battery_stubs -Itests/zmk/wired_stubs -Itests/zmk/clock_stubs \
  -Ifirmware/zmk/wired -Ifirmware/zmk/radio -Ifirmware/zmk/soc/westberry/wb32fq95 \
  tests/zmk/battery_radio_uart_test.c firmware/zmk/radio/core.c \
  -o artifacts/tests/zmk/battery-radio-uart
artifacts/tests/zmk/battery-radio-uart

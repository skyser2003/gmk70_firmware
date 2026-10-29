#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wsign-conversion -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  -DCONFIG_GMK70_BATTERY=1 -Itests/zmk/battery_stubs -Itests/zmk/wired_stubs \
  -Itests/zmk/clock_stubs -Ifirmware/zmk/wired -Ifirmware/zmk/soc/westberry/wb32fq95 \
  tests/zmk/battery_test.c -o artifacts/tests/zmk/battery
artifacts/tests/zmk/battery
cc -std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Ifirmware/zmk/battery \
  tests/zmk/battery_disabled_test.c -o artifacts/tests/zmk/battery-disabled
artifacts/tests/zmk/battery-disabled

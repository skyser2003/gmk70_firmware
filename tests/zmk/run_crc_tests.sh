#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wsign-conversion \
  -fno-pie -no-pie -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -Itests/zmk/crc_stubs tests/zmk/crc_ieee_test.c \
  dependencies/zephyr/lib/crc/crc32_sw.c firmware/zmk/runtime/crc.c \
  firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c \
  -o artifacts/tests/zmk/crc-ieee
artifacts/tests/zmk/crc-ieee

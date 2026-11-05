#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wsign-conversion -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  -Ifirmware/zmk/power tests/zmk/power_core_test.c firmware/zmk/power/core.c \
  -o artifacts/tests/zmk/power-core
artifacts/tests/zmk/power-core

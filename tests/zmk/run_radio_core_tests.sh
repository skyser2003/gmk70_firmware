#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wsign-conversion -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  -Ifirmware/zmk/radio tests/zmk/radio_core_test.c firmware/zmk/radio/core.c \
  -o artifacts/tests/zmk/radio-core
artifacts/tests/zmk/radio-core

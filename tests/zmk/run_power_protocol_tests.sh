#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wsign-conversion -fanalyzer -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  -Ifirmware/zmk/power -Ifirmware/zmk/wired tests/zmk/power_protocol_test.c \
  firmware/zmk/power/protocol.c firmware/zmk/wired/wired_protocol.c \
  -o artifacts/tests/zmk/power-protocol
artifacts/tests/zmk/power-protocol

#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
for metadata in 0 1; do
  cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wsign-conversion -fanalyzer -fno-pie -no-pie \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -DCONFIG_ZMK_BEHAVIOR_METADATA="$metadata" -Itests/zmk/controls_behavior_stubs \
    -Itests/zmk/radio_behavior_stubs -Ifirmware/zmk/include \
    tests/zmk/controls_behavior_test.c -o "artifacts/tests/zmk/controls-behavior-$metadata"
  "artifacts/tests/zmk/controls-behavior-$metadata"
done

#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Itests/zmk/native_encoder_stubs -Itests/zmk/wired_stubs \
    -Itests/zmk/radio_native_stubs -Ifirmware/zmk/wired \
    tests/zmk/native_encoder_test.c firmware/zmk/wired/encoder.c \
    -o artifacts/tests/zmk/native-encoder
artifacts/tests/zmk/native-encoder

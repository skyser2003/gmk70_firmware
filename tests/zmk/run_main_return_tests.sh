#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Itests/zmk/main_return_stubs tests/zmk/main_return_test.c \
    firmware/zmk/trial/main_return.c -o artifacts/tests/zmk/main-return
artifacts/tests/zmk/main-return

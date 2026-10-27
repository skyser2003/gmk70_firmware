#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Itests/zmk/clock_stubs -Ifirmware/zmk/soc/westberry/wb32fq95 \
    tests/zmk/soc_clock_wb32_test.c firmware/zmk/soc/westberry/wb32fq95/soc.c \
    -o artifacts/tests/zmk/soc-clock
artifacts/tests/zmk/soc-clock

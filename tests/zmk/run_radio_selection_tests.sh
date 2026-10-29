#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    tests/zmk/radio_selection_test.c firmware/zmk/radio/selection.c \
    -o .work/zmk/tests/radio-selection
.work/zmk/tests/radio-selection

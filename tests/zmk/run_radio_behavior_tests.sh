#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests/radio-behavior
for metadata in 0 1; do
    cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
        -DCONFIG_ZMK_BEHAVIOR_METADATA="$metadata" -Itests/zmk/radio_behavior_stubs \
        tests/zmk/radio_behavior_test.c -o ".work/zmk/tests/radio-behavior/metadata-$metadata"
    ".work/zmk/tests/radio-behavior/metadata-$metadata"
done

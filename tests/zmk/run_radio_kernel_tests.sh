#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
for profile in normal qualification; do
    flags=()
    if [[ "$profile" == qualification ]]; then flags+=(-DCONFIG_GMK70_RADIO_QUALIFICATION=1); fi
    cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
        "${flags[@]}" -Itests/zmk/radio_kernel_stubs -Itests/zmk/radio_behavior_stubs \
        -Itests/zmk/radio_endpoint_stubs \
        tests/zmk/radio_kernel_test.c firmware/zmk/radio/core.c firmware/zmk/radio/selection.c \
        -o ".work/zmk/tests/radio-kernel-$profile"
    ".work/zmk/tests/radio-kernel-$profile"
done

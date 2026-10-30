#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    tests/zmk/function_flip_core_test.c firmware/zmk/function_flip/core.c -o .work/zmk/tests/function-flip-core
.work/zmk/tests/function-flip-core
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    -Itests/zmk/function_flip_stubs -Itests/zmk/radio_kernel_stubs \
    -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
    tests/zmk/function_flip_adapter_test.c firmware/zmk/function_flip/core.c -o .work/zmk/tests/function-flip-adapter
.work/zmk/tests/function-flip-adapter
for metadata in 0 1; do
    cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
        -DCONFIG_ZMK_BEHAVIOR_METADATA="$metadata" -Itests/zmk/radio_behavior_stubs \
        tests/zmk/function_flip_behavior_test.c -o ".work/zmk/tests/function-flip-behavior-$metadata"
    ".work/zmk/tests/function-flip-behavior-$metadata"
done

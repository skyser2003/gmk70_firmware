#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/qmk
for framework in qmk vial-qmk; do
    adapter=()
    if [[ "${GMK70_TEST_IDLE_BASELINE:-no}" != yes ]]; then
        adapter+=(firmware/qmk-common/keyboards/zuoya/gmk70/matrix_idle.c
            -Wl,--wrap=matrix_scan -Wl,--wrap=matrix_init)
    fi
    cc -std=gnu11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
        -fsanitize=undefined -fno-sanitize-recover=all \
        -include tests/qmk/matrix_idle_stubs/config.h -D'QMK_KEYBOARD_H="quantum.h"' \
        -Itests/qmk/matrix_idle_stubs -I"dependencies/$framework/quantum" \
        -I"dependencies/$framework/quantum/split_common" \
        -I"dependencies/$framework/platforms" -I"dependencies/$framework/quantum/debounce" \
        tests/qmk/matrix_idle_test.c tests/qmk/matrix_idle_stop_fixture.c \
        "dependencies/$framework/quantum/matrix.c" \
        "dependencies/$framework/quantum/matrix_common.c" \
        "dependencies/$framework/quantum/debounce/sym_defer_g.c" \
        "${adapter[@]}" -o "artifacts/tests/qmk/matrix-idle-$framework"
    for half in left right; do
        for role in master slave; do
            "artifacts/tests/qmk/matrix-idle-$framework" "$half" "$role"
        done
    done
done

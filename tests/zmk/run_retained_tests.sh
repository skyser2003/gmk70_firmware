#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
args=(-DCONFIG_GMK70_RETAINED_SLEEP=1 -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all
    -Itests/zmk/retained_stubs -Itests/zmk/clock_stubs -Idependencies/zephyr/include
    -Ifirmware/qmk-common/drivers/wireless -Ifirmware/zmk/soc/westberry/wb32fq95)
cc "${args[@]}" -Wno-unused-parameter -Wno-error=sign-compare -c tests/zmk/retained_systick_native.c -o artifacts/tests/zmk/retained-systick.o
cc "${args[@]}" -Wno-unused-parameter -Wno-error=type-limits -Wno-error=sign-compare -c dependencies/zephyr/kernel/timeout.c -o artifacts/tests/zmk/retained-timeout.o
cc "${args[@]}" tests/zmk/retained_test.c firmware/zmk/power/retained.c firmware/zmk/power/retained_clock.c \
    artifacts/tests/zmk/retained-systick.o artifacts/tests/zmk/retained-timeout.o \
    -Wl,--wrap=sys_clock_cycle_get_32 -Wl,--wrap=sys_clock_cycle_get_64 \
    -o artifacts/tests/zmk/retained
for scenario in normal 20k 40k hours long wrap fraction read-rollover coincidence kernel-pending owned foreign foreign-lsi foreign-high reset-option; do
    artifacts/tests/zmk/retained "$scenario"
done

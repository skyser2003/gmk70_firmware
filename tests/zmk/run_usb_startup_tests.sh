#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p .work/zmk/tests
for profile in legacy wired; do
    wired=0
    if [[ "$profile" == wired ]]; then wired=1; fi
    manifest="$PWD/.work/zmk/tests/usb-startup-$profile.sources"
    firmware/zmk/tools/.venv/bin/cmake -DCONFIG_BOARD_GMK70_WIRED="$wired" -DOUTPUT="$manifest" \
        -P tests/zmk/usb_startup_sources.cmake
    mapfile -t sources < "$manifest"
    cc -std=c11 -Wall -Wextra -Werror -g -O1 \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -DEXPECT_WIRED="$wired" -Itests/zmk/usb_startup_stubs \
        -Itests/zmk/stubs -Idependencies/zephyr/include \
        tests/zmk/usb_startup_test.c "${sources[@]}" \
        -o ".work/zmk/tests/usb-startup-$profile"
    ".work/zmk/tests/usb-startup-$profile"
done

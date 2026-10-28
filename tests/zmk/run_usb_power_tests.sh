#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p .work/zmk/tests
flags=(-Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
for profile in legacy wired; do
    wired=0
    if [[ "$profile" == wired ]]; then wired=1; fi
    manifest="$PWD/.work/zmk/tests/usb-power-$profile.sources"
    firmware/zmk/tools/.venv/bin/cmake -DCONFIG_BOARD_GMK70_WIRED="$wired" \
        -DOUTPUT="$manifest" -P tests/zmk/usb_power_sources.cmake
    objects=()
    while IFS= read -r source; do
        obj=".work/zmk/tests/usb-power-$profile-$(basename "$source").o"
        includes=(-Itests/zmk/usb_power_stubs -Itests/zmk/stubs)
        if [[ "$source" == */usb_dc_wb32.c ]]; then includes=(-Itests/zmk/stubs); fi
        cc -std=c11 "${flags[@]}" "${includes[@]}" -Idependencies/zephyr/include -c "$source" -o "$obj"
        objects+=("$obj")
    done < "$manifest"
    c++ -std=c++20 "${flags[@]}" -DEXPECT_WIRED="$wired" \
        -Itests/zmk/usb_power_stubs -Idependencies/zephyr/include \
        tests/zmk/usb_power_test.cpp "${objects[@]}" -o ".work/zmk/tests/usb-power-$profile"
    ".work/zmk/tests/usb-power-$profile"
done

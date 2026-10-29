#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
flags=(
    -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
    -fno-pie -no-pie -Itests/zmk/update_stubs -Ifirmware/zmk/update
    -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update
)
for profile in permanent qualification studio watchdog-rollback; do
    profile_flags=()
    image_end=0x08002301
    if [[ "$profile" == qualification ]]; then
        profile_flags+=(-DCONFIG_GMK70_STAGING_QUALIFICATION=1)
        image_end=0x08020301
    fi
    if [[ "$profile" == studio ]]; then profile_flags+=(-DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1); fi
    if [[ "$profile" == watchdog-rollback ]]; then profile_flags+=(-DCONFIG_GMK70_WATCHDOG_ROLLBACK=1); fi
    cc "${flags[@]}" "${profile_flags[@]}" "-Wl,--defsym=__gmk_image_end=$image_end" \
        tests/zmk/update_platform_wb32_test.c firmware/zmk/update/wb32_flash.c \
        firmware/zmk/update/wb32_watchdog.c -o "artifacts/tests/zmk/update-platform-$profile"
    "artifacts/tests/zmk/update-platform-$profile"
done

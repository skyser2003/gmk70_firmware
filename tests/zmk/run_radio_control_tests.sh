#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
for profile in canonical qualification; do
    flags=()
    if [[ "$profile" == qualification ]]; then flags+=(-DCONFIG_GMK70_RADIO_QUALIFICATION=1); fi
    cc -std=c11 -O1 -g -Wall -Wextra -Werror -fno-pie -no-pie \
      -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
      -ffunction-sections -fdata-sections -Wl,--gc-sections "${flags[@]}" \
      -Ifirmware/zmk/radio -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
      tests/zmk/radio_control_test.c firmware/zmk/radio/control.c \
      firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c -o "artifacts/tests/zmk/radio-control-$profile"
    "artifacts/tests/zmk/radio-control-$profile"
done

#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Ifirmware/zmk/wired -Idependencies/vial-qmk/quantum \
    tests/zmk/wired_test.c firmware/zmk/wired/wired_protocol.c dependencies/vial-qmk/quantum/crc.c \
    -o artifacts/tests/zmk/wired
artifacts/tests/zmk/wired
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Ifirmware/zmk/wired tests/zmk/wired_master_test.c firmware/zmk/wired/wired_protocol.c \
    -o artifacts/tests/zmk/wired-master
artifacts/tests/zmk/wired-master
firmware/zmk/tools/.venv/bin/python firmware/zmk/tools/wired_pins.py \
    --keyboard firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json \
    --output .work/zmk/wired-native/normal/gmk70_wired_pins.h
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Itests/zmk/wired_stubs -Itests/zmk/clock_stubs \
    -Ifirmware/zmk/wired -Ifirmware/zmk/soc/westberry/wb32fq95 \
    -I.work/zmk/wired-native/normal \
    tests/zmk/wired_wb32_test.c firmware/zmk/wired/wired_protocol.c \
    -o artifacts/tests/zmk/wired-wb32
artifacts/tests/zmk/wired-wb32
cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    -Itests/zmk/wired_stubs -Ifirmware/zmk/wired \
    tests/zmk/wired_kscan_test.c -o artifacts/tests/zmk/wired-kscan
artifacts/tests/zmk/wired-kscan

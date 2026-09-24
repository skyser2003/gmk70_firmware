#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
mkdir -p artifacts/tests/qmk
common_flags=(
    -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter
    -fsanitize=address,undefined -fno-omit-frame-pointer
    -Itests/qmk/stubs -Itests/qmk -Ifirmware/qmk-common/drivers/wireless
    -Idependencies/qmk/quantum -Idependencies/qmk/quantum/keymap_extras -Idependencies/qmk/quantum/sequencer -Idependencies/qmk/tmk_core/protocol
)
cc "${common_flags[@]}" -Ifirmware/qmk-common/keyboards/zuoya/gmk70/wls \
    tests/qmk/wireless_mode_test.c -o artifacts/tests/qmk/wireless-mode
artifacts/tests/qmk/wireless-mode
cc "${common_flags[@]}" -include firmware/qmk-common/keyboards/zuoya/gmk70/config.h -DSPLIT_HAND_PIN=B9 \
    tests/qmk/split_role_test.c firmware/qmk-common/keyboards/zuoya/gmk70/split_role.c \
    -o artifacts/tests/qmk/split-role
artifacts/tests/qmk/split-role
for endpoint in dedicated shared; do
    endpoint_flags=()
    if [[ "$endpoint" == shared ]]; then
        endpoint_flags+=(-DKEYBOARD_SHARED_EP)
    fi
    cc "${common_flags[@]}" -DRAW_ENABLE "${endpoint_flags[@]}" \
        tests/qmk/wireless_test.c tests/qmk/radio_fixture.c \
        firmware/qmk-common/drivers/wireless/wireless.c firmware/qmk-common/drivers/wireless/md_raw.c \
        -Wl,--wrap=raw_hid_send -o "artifacts/tests/qmk/wireless-$endpoint"
    "artifacts/tests/qmk/wireless-$endpoint"
done
cc "${common_flags[@]}" -D'QMK_KEYBOARD_H="quantum.h"' \
    -Ifirmware/qmk-common/keyboards/zuoya/gmk70 \
    tests/qmk/stock_keycodes_test.c firmware/qmk-common/keyboards/zuoya/gmk70/stock_keycodes.c \
    -o artifacts/tests/qmk/stock-keycodes
artifacts/tests/qmk/stock-keycodes
cc "${common_flags[@]}" tests/qmk/stock_via_test.c \
    firmware/qmk-via/compat/stock_via.c -o artifacts/tests/qmk/stock-via
artifacts/tests/qmk/stock-via
python3 -m unittest discover -s tools/keymap-migration/tests -v
python3 -m unittest discover -s tests/repository -v

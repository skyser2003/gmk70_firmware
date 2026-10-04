#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
mkdir -p artifacts/tests/qmk
common_flags=(
    -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter
    -fsanitize=address,undefined -fno-omit-frame-pointer
    -Itests/qmk/stubs -Itests/qmk -Ifirmware/qmk-common/drivers/wireless
    -Ifirmware/qmk-common/keyboards/zuoya/gmk70
    -Idependencies/qmk/quantum -Idependencies/qmk/quantum/keymap_extras -Idependencies/qmk/quantum/sequencer -Idependencies/qmk/tmk_core/protocol
)
cc "${common_flags[@]}" tests/qmk/update_test.c \
    firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c \
    firmware/qmk-common/keyboards/zuoya/gmk70/update/protocol.c \
    -o artifacts/tests/qmk/updater
artifacts/tests/qmk/updater
cc -Itests/qmk/lowpower_stubs "${common_flags[@]}" -DRGB_MATRIX_ENABLE \
    tests/qmk/lowpower_test.c firmware/qmk-common/drivers/wireless/lowpower.c \
    -o artifacts/tests/qmk/lowpower
artifacts/tests/qmk/lowpower
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
cc "${common_flags[@]}" -Itests/qmk/stock_battery_stubs \
    -include firmware/qmk-common/keyboards/zuoya/gmk70/config.h \
    tests/qmk/stock_battery_test.c firmware/qmk-common/keyboards/zuoya/gmk70/stock_battery.c \
    -o artifacts/tests/qmk/stock-battery
artifacts/tests/qmk/stock-battery
cc "${common_flags[@]}" tests/qmk/stock_via_test.c \
    firmware/qmk-via/compat/stock_via.c -o artifacts/tests/qmk/stock-via
artifacts/tests/qmk/stock-via
for profile in release debug; do
    profile_flags=()
    if [[ "$profile" == debug ]]; then
        profile_flags+=(-DGMK70_DEBUG_USB_BOOTLOADER)
    fi
    cc "${common_flags[@]}" -Itests/qmk/bootloader_stubs -Itests/qmk/lowpower_stubs \
        -DRAW_ENABLE "${profile_flags[@]}" tests/qmk/usb_bootloader_test.c \
        firmware/qmk-via/compat/stock_via.c firmware/qmk-common/drivers/wireless/md_raw.c \
        -Wl,--wrap=raw_hid_send -o "artifacts/tests/qmk/usb-bootloader-$profile"
    "artifacts/tests/qmk/usb-bootloader-$profile"
    cc -Itests/qmk/update_stubs "${common_flags[@]}" -Itests/qmk/bootloader_stubs -Itests/qmk/lowpower_stubs \
        -DRAW_ENABLE -DGMK70_SPLIT_UPDATE "${profile_flags[@]}" tests/qmk/update_adapter_test.c \
        firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c \
        firmware/qmk-common/keyboards/zuoya/gmk70/update/protocol.c \
        firmware/qmk-common/keyboards/zuoya/gmk70/update/qmk_update.c \
        firmware/qmk-via/compat/stock_via.c firmware/qmk-common/drivers/wireless/md_raw.c \
        -Wl,--wrap=raw_hid_send -o "artifacts/tests/qmk/update-adapter-$profile"
    "artifacts/tests/qmk/update-adapter-$profile"
done
cc "${common_flags[@]}" -Itests/qmk/split_diagnostic_stubs -DGMK70_SPLIT_DIAGNOSTICS \
    tests/qmk/split_diagnostics_test.c \
    firmware/qmk-common/keyboards/zuoya/gmk70/split_diagnostics.c \
    firmware/qmk-via/compat/stock_via.c \
    -o artifacts/tests/qmk/split-diagnostics
artifacts/tests/qmk/split-diagnostics
cc -Itests/qmk/output_stubs -Idependencies/qmk/drivers/led "${common_flags[@]}" \
    -include stdint.h -include firmware/qmk-common/keyboards/zuoya/gmk70/config.h \
    -DWB32FQ95xx -DWS2812_DI_PIN=15 -DRGB_MATRIX_WS2812 -DRGB_MATRIX_LED_COUNT=77 \
    tests/qmk/led_output_test.c \
    dependencies/qmk/platforms/chibios/drivers/ws2812_spi.c \
    -o artifacts/tests/qmk/led-output
artifacts/tests/qmk/led-output
python3 -m unittest discover -s tools/keymap-migration/tests -v
python3 -m unittest discover -s tests/repository -v

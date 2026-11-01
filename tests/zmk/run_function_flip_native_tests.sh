#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests
for radio in 0 1; do
    for separate_mod_release in 0 1; do
        flags=(-std=c11 -D_XOPEN_SOURCE=700 -pthread -Wall -Wextra -Werror -O1 -g -fno-pie -fsanitize=address,undefined
            -DCONFIG_ZMK_HID_REPORT_TYPE_NKRO=1 -DCONFIG_ZMK_HID_REPORT_TYPE_HKRO=0
            -DCONFIG_GMK70_FUNCTION_FLIP=1 -DCONFIG_ZMK_USB_BOOT=1 -DCONFIG_ZMK_POINTING=0
            -DCONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6
            -DCONFIG_ZMK_HID_SEPARATE_MOD_RELEASE_REPORT="$separate_mod_release"
            -include tests/zmk/function_flip_native_prelude.h -Itests/zmk/function_flip_native_stubs
            -Itests/zmk/function_flip_stubs -Itests/zmk/radio_kernel_stubs
            -Itests/zmk/radio_endpoint_stubs -Idependencies/zmk/app/include -Ifirmware/zmk/update
            -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update
            -Ifirmware/zmk/nkro -Ifirmware/qmk-common/drivers/wireless)
        sources=()
        if [[ "$radio" == 1 ]]; then
            flags+=(-DCONFIG_GMK70_RADIO=1)
            sources+=(firmware/zmk/radio/endpoint.c)
        fi
        # Pinned upstream boot packing uses int versus sizeof; do not edit upstream.
        cc "${flags[@]}" -Wno-sign-compare -c dependencies/zmk/app/src/hid.c \
            -o ".work/zmk/tests/function-flip-native-hid-$separate_mod_release.o"
        cc "${flags[@]}" -no-pie \
            tests/zmk/function_flip_native_test.c ".work/zmk/tests/function-flip-native-hid-$separate_mod_release.o" \
            firmware/zmk/function_flip/core.c firmware/zmk/studio/bool_setting.c \
            tests/zmk/function_flip_native_nkro.c "${sources[@]}" \
            firmware/qmk-common/drivers/wireless/radio_keys.c \
            -Wl,--wrap=zmk_hid_get_keyboard_report -Wl,--wrap=zmk_hid_get_boot_report \
            -Wl,--wrap=zmk_hid_implicit_modifiers_press -Wl,--wrap=zmk_hid_implicit_modifiers_release \
            -Wl,--wrap=zmk_hid_keyboard_clear -Wl,--wrap=zmk_hid_consumer_clear \
            -Wl,--wrap=zmk_hid_press -Wl,--wrap=zmk_hid_release \
            -Wl,--wrap=zmk_hid_is_pressed -Wl,--wrap=zmk_hid_register_mods \
            -o ".work/zmk/tests/function-flip-native-$radio-$separate_mod_release"
        ".work/zmk/tests/function-flip-native-$radio-$separate_mod_release"
    done
done

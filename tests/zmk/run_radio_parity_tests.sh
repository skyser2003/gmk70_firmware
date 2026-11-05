#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wsign-conversion -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  -DCONFIG_ZMK_POINTING=1 -Ifirmware/zmk/radio \
  tests/zmk/radio_parity_test.c firmware/zmk/radio/core.c -o artifacts/tests/zmk/radio-parity
artifacts/tests/zmk/radio-parity
native_flags=(-std=c11 -D_XOPEN_SOURCE=700 -pthread -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined
  -DCONFIG_ZMK_HID_REPORT_TYPE_NKRO=0 -DCONFIG_ZMK_HID_REPORT_TYPE_HKRO=1
  -DCONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE=6 -DCONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6
  -DCONFIG_ZMK_USB_BOOT=0 -DCONFIG_ZMK_POINTING=1 -DCONFIG_ZMK_HID_SEPARATE_MOD_RELEASE_REPORT=0
  -include tests/zmk/function_flip_native_prelude.h -Itests/zmk/radio_mouse_stubs
  -Itests/zmk/function_flip_native_stubs -Itests/zmk/radio_endpoint_stubs
  -Idependencies/zmk/app/include -Ifirmware/qmk-common/drivers/wireless)
cc "${native_flags[@]}" -c dependencies/zmk/app/src/hid.c -o artifacts/tests/zmk/radio-mouse-native-hid.o
cc "${native_flags[@]}" tests/zmk/radio_mouse_endpoint_test.c tests/zmk/radio_mouse_endpoint_upstream.c artifacts/tests/zmk/radio-mouse-native-hid.o \
  firmware/zmk/radio/endpoint.c firmware/zmk/radio/core.c \
  -Wl,--wrap=zmk_endpoint_send_report -Wl,--wrap=zmk_endpoint_send_mouse_report \
  -Wl,--wrap=zmk_hid_keyboard_clear -Wl,--wrap=zmk_hid_consumer_clear -Wl,--wrap=zmk_hid_mouse_clear \
  -o artifacts/tests/zmk/radio-mouse-endpoint
artifacts/tests/zmk/radio-mouse-endpoint
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
  -ffunction-sections -Wl,--gc-sections -DCONFIG_GMK70_CONTROLS=1 \
  -Itests/zmk/radio_kernel_stubs -Itests/zmk/radio_behavior_stubs -Itests/zmk/radio_endpoint_stubs \
  tests/zmk/radio_kernel_test.c firmware/zmk/radio/core.c firmware/zmk/radio/selection.c \
  -o artifacts/tests/zmk/radio-controls-parity
artifacts/tests/zmk/radio-controls-parity

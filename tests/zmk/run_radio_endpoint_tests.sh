#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p .work/zmk/tests/radio-endpoint
sources=(tests/zmk/radio_endpoint_test.c tests/zmk/radio_endpoint_upstream.c tests/zmk/radio_endpoint_hid.c firmware/zmk/radio/endpoint.c)
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fno-pie -no-pie -fsanitize=address,undefined \
    -DCONFIG_ZMK_HID_REPORT_TYPE_HKRO=1 -DCONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE=6 \
    -DCONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6 \
    -Itests/zmk/radio_endpoint_stubs -Idependencies/zmk/app/include \
    "${sources[@]}" \
    -Wl,--wrap=zmk_endpoint_send_report -Wl,--wrap=zmk_hid_keyboard_clear \
    -Wl,--wrap=zmk_hid_consumer_clear -o .work/zmk/tests/radio-endpoint/endpoint
.work/zmk/tests/radio-endpoint/endpoint
for report_size in 5 7; do
    if cc -std=c11 -Wall -Wextra -Werror -fsyntax-only \
        -DCONFIG_ZMK_HID_REPORT_TYPE_HKRO=1 -DCONFIG_ZMK_HID_KEYBOARD_REPORT_SIZE="$report_size" \
        -DCONFIG_ZMK_HID_CONSUMER_REPORT_SIZE=6 \
        -Itests/zmk/radio_endpoint_stubs -Idependencies/zmk/app/include \
        firmware/zmk/radio/endpoint.c >.work/zmk/tests/radio-endpoint/rejected-config.log 2>&1; then
        printf 'Radio endpoint accepted unsupported keyboard report size %s\n' "$report_size" >&2
        exit 1
    fi
done

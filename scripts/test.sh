#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -f .env ]]; then
    set -a
    source .env
    set +a
elif [[ -f .envrc ]]; then
    source .envrc
fi
mkdir -p artifacts/tests
for endpoint in dedicated shared; do
    endpoint_flags=()
    if [[ "$endpoint" == shared ]]; then
        endpoint_flags+=(-DKEYBOARD_SHARED_EP)
    fi
    cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter \
        -fsanitize=address,undefined -fno-omit-frame-pointer -DRAW_ENABLE \
        "${endpoint_flags[@]}" -Itests/stubs -Itests -Ikeyboards/linker/wireless \
        -Iqmk_firmware/quantum -Iqmk_firmware/tmk_core/protocol \
        tests/wireless_test.c tests/radio_fixture.c \
        keyboards/linker/wireless/wireless.c keyboards/linker/wireless/md_raw.c \
        -Wl,--wrap=raw_hid_send -o "artifacts/tests/wireless-$endpoint"
    "artifacts/tests/wireless-$endpoint"
done

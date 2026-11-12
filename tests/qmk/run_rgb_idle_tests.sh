#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/qmk
for framework in qmk vial-qmk; do
    for leds in 36 41; do
        cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fno-pie -no-pie \
            -fsanitize=address,undefined -fno-sanitize-recover=all \
            -Itests/qmk/output_stubs -I"dependencies/$framework/drivers/led" \
            -Ifirmware/qmk-common/keyboards/zuoya/gmk70 -Idependencies/qmk/quantum \
            -include stdint.h -include firmware/qmk-common/keyboards/zuoya/gmk70/config.h \
            -DWB32FQ95xx -DWS2812_DI_PIN=15 -DRGB_MATRIX_WS2812 -DRGB_MATRIX_LED_COUNT="$leds" \
            tests/qmk/rgb_idle_test.c firmware/qmk-common/keyboards/zuoya/gmk70/rgb_idle.c \
            "dependencies/$framework/platforms/chibios/drivers/ws2812_spi.c" \
            -Wl,--wrap=ws2812_init -Wl,--wrap=ws2812_flush \
            -o "artifacts/tests/qmk/rgb-idle-$framework-$leds"
        "artifacts/tests/qmk/rgb-idle-$framework-$leds"
    done
done

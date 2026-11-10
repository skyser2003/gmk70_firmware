#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
firmware/zmk/tools/.venv/bin/python firmware/zmk/tools/wired_pins.py \
  --keyboard firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json \
  --output .work/zmk/wired-native/normal/gmk70_wired_pins.h
cc -std=c11 -Wall -Wextra -Werror -fno-pie -no-pie -fsanitize=address,undefined \
  -DCONFIG_GMK70_EVENT_IDLE=1 -Ifirmware/zmk/wired \
  tests/zmk/event_link_test.c firmware/zmk/wired/event_link.c firmware/zmk/wired/wired_protocol.c \
  -o artifacts/tests/zmk/event-link
artifacts/tests/zmk/event-link
cc -std=c11 -Wall -Wextra -Werror -fno-pie -no-pie -fsanitize=address,undefined \
  -DCONFIG_GMK70_EVENT_IDLE=1 -DCONFIG_GMK70_RADIO=1 -DCONFIG_GMK70_NATIVE_UPDATE_LEGACY_BRIDGE=1 \
  -Itests/zmk/native_encoder_stubs -Itests/zmk/wired_stubs -Itests/zmk/clock_stubs -Itests/zmk/radio_native_stubs \
  -Ifirmware/zmk/wired -Ifirmware/zmk/soc/westberry/wb32fq95 -I.work/zmk/wired-native/normal \
  tests/zmk/event_wb32_test.c firmware/zmk/wired/event_link.c firmware/zmk/wired/wired_protocol.c firmware/zmk/wired/encoder.c \
  -o artifacts/tests/zmk/event-wb32
artifacts/tests/zmk/event-wb32
cc -std=c11 -Wall -Wextra -Werror -fno-pie -no-pie -fsanitize=address,undefined \
  -DCONFIG_GMK70_EVENT_IDLE=1 -DCONFIG_GMK70_BATTERY=1 \
  -Itests/zmk/battery_stubs -Itests/zmk/wired_stubs -Itests/zmk/clock_stubs \
  -Ifirmware/zmk/wired -Ifirmware/zmk/soc/westberry/wb32fq95 \
  tests/zmk/event_battery_test.c -o artifacts/tests/zmk/event-battery
artifacts/tests/zmk/event-battery
cc -std=c11 -Wall -Wextra -Werror -fno-pie -no-pie -fsanitize=address,undefined \
  -DCONFIG_GMK70_EVENT_IDLE=1 -Itests/zmk/idle_stubs \
  tests/zmk/event_idle_test.c -o artifacts/tests/zmk/event-idle
artifacts/tests/zmk/event-idle

cc -std=c11 -Wall -Wextra -Werror -fno-pie -no-pie -fsanitize=address,undefined \
  -DCONFIG_GMK70_EVENT_IDLE=1 -DCONFIG_GMK70_RETAINED_SLEEP=1 -DCONFIG_GMK70_RADIO=1 -DCONFIG_GMK70_NATIVE_UPDATE_LEGACY_BRIDGE=1 \
  -Itests/zmk/native_encoder_stubs -Itests/zmk/wired_stubs -Itests/zmk/clock_stubs -Itests/zmk/radio_native_stubs \
  -Ifirmware/zmk/wired -Ifirmware/zmk/soc/westberry/wb32fq95 -I.work/zmk/wired-native/normal \
  tests/zmk/event_wb32_test.c firmware/zmk/wired/event_link.c firmware/zmk/wired/wired_protocol.c firmware/zmk/wired/encoder.c \
  -o artifacts/tests/zmk/event-wb32-retained
artifacts/tests/zmk/event-wb32-retained

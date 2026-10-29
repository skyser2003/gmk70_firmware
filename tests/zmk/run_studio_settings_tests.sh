#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -Ifirmware/zmk/studio -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
  tests/zmk/settings_store_test.c firmware/zmk/studio/settings_store.c \
  firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c \
  -o artifacts/tests/zmk/studio-settings
artifacts/tests/zmk/studio-settings
for profile in permanent watchdog-rollback; do
flags=()
if [[ "$profile" == watchdog-rollback ]]; then flags+=(-DCONFIG_GMK70_WATCHDOG_ROLLBACK=1); fi
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -pthread -fno-pie -no-pie "${flags[@]}" \
  -fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -Wl,--gc-sections \
  -Itests/zmk/studio_stubs -Ifirmware/zmk/studio -Ifirmware/zmk/update \
  -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
  -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1 tests/zmk/settings_backend_test.c \
  firmware/zmk/studio/settings_backend.c firmware/zmk/studio/settings_store.c \
  firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c \
  -o "artifacts/tests/zmk/studio-backend-$profile"
"artifacts/tests/zmk/studio-backend-$profile"
done

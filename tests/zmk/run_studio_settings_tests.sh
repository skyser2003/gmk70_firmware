#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
for profile in standalone updater; do
flags=()
if [[ "$profile" == updater ]]; then flags+=(-DCONFIG_GMK70_ZMK_UPDATER=1); fi
cc -std=c11 -Wall -Wextra -Werror -pthread -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-sanitize-recover=all "${flags[@]}" \
  -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1 -Itests/zmk/studio_stubs \
  -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
  tests/zmk/storage_lease_test.c firmware/zmk/runtime/storage.c \
  -o "artifacts/tests/zmk/storage-lease-$profile"
"artifacts/tests/zmk/storage-lease-$profile"
done
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-builtin-memcmp -Wl,--wrap=memcmp -Wl,--wrap=gmk_crc32 \
  -fno-omit-frame-pointer -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -Ifirmware/zmk/studio -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
  tests/zmk/settings_store_test.c firmware/zmk/studio/settings_store.c \
  firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c \
  -o artifacts/tests/zmk/studio-settings
artifacts/tests/zmk/studio-settings
for power in 0 1 ids; do
power_flags=()
if [[ "$power" != 0 ]]; then power_flags+=(-DCONFIG_GMK70_POWER=1); fi
if [[ "$power" == ids ]]; then power_flags+=(-DCONFIG_ZMK_BEHAVIOR_LOCAL_IDS=1 -DCONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE=1); fi
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -pthread -fno-pie -no-pie "${power_flags[@]}" \
  -fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -Wl,--gc-sections \
  -Itests/zmk/studio_stubs -Ifirmware/zmk/studio -Ifirmware/zmk/update \
  -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
  -DCONFIG_GMK70_ZMK_STUDIO_SETTINGS=1 tests/zmk/settings_backend_test.c \
  firmware/zmk/studio/settings_backend.c firmware/zmk/studio/settings_store.c \
  firmware/qmk-common/keyboards/zuoya/gmk70/update/image.c \
  -o "artifacts/tests/zmk/studio-backend-$power"
"artifacts/tests/zmk/studio-backend-$power"
done

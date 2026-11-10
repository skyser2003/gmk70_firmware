#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
for metadata in 0 1; do
 for endpoint in usb radio; do
  endpoint_flags=()
  [[ "$endpoint" != radio ]] || endpoint_flags+=(-DCONFIG_GMK70_RADIO=1)
  cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wsign-conversion -fno-pie -no-pie \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -DCONFIG_GMK70_MERGED_CONTROLS=1 -DCONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS=1 \
    "${endpoint_flags[@]}" \
    -DCONFIG_ZMK_SPLIT=1 -DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=1 -DCONFIG_ZMK_BEHAVIOR_METADATA="$metadata" \
    -Itests/zmk/merged_controls_stubs -Itests/zmk/controls_behavior_stubs \
    -Itests/zmk/recovery_stubs -Itests/zmk/radio_behavior_stubs -Ifirmware/zmk/include \
    tests/zmk/merged_controls_test.c -o "artifacts/tests/zmk/merged-controls-$endpoint-$metadata"
  "artifacts/tests/zmk/merged-controls-$endpoint-$metadata"
 done
done

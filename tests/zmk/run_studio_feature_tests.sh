#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
mkdir -p artifacts/tests/zmk
cc -std=c11 -Wall -Wextra -Werror -fno-pie -no-pie -fsanitize=address,undefined \
  -Itests/zmk/receiver_stubs -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
  tests/zmk/studio_feature_test.c firmware/zmk/update/studio_feature.c -o artifacts/tests/zmk/studio-feature
artifacts/tests/zmk/studio-feature

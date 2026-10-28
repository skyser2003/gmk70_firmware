#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
for profile in permanent qualification relay; do
    flags=()
    if [[ "$profile" == qualification ]]; then flags+=(-DCONFIG_GMK70_STAGING_QUALIFICATION=1); fi
    if [[ "$profile" == relay ]]; then flags+=(-DCONFIG_GMK70_ZMK_PEER_TRIAL_RELAY=1); fi
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie \
        -Itests/zmk/receiver_stubs -Ifirmware/zmk/update -Ifirmware/zmk/wired -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update "${flags[@]}" \
        tests/zmk/receiver_test.c firmware/zmk/update/receiver.c firmware/qmk-common/keyboards/zuoya/gmk70/update/{image,protocol,trial}.c -o "artifacts/tests/zmk/receiver-$profile"
    "artifacts/tests/zmk/receiver-$profile"
done
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie \
    -Itests/zmk/receiver_stubs -Ifirmware/zmk/update -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
    tests/zmk/raw_hid_test.c firmware/zmk/update/raw_hid.c -o artifacts/tests/zmk/raw-hid
artifacts/tests/zmk/raw-hid
for profile in permanent qualification; do
    flags=()
    if [[ "$profile" == qualification ]]; then flags+=(-DCONFIG_GMK70_STAGING_QUALIFICATION=1); fi
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie \
        -Itests/zmk/receiver_stubs -Ifirmware/zmk/update -Ifirmware/zmk/wired -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update "${flags[@]}" \
        tests/zmk/service_test.c -o "artifacts/tests/zmk/update-service-$profile"
    "artifacts/tests/zmk/update-service-$profile"
done
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie \
    -Itests/zmk/receiver_stubs -Ifirmware/zmk/update -Ifirmware/zmk/wired -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update \
    tests/zmk/service_boundary_test.c firmware/zmk/update/receiver.c firmware/qmk-common/keyboards/zuoya/gmk70/update/{image,protocol}.c \
    -o artifacts/tests/zmk/service-boundary
artifacts/tests/zmk/service-boundary
artifacts/tests/zmk/service-boundary gates
for profile in permanent qualification; do
    flags=()
    if [[ "$profile" == qualification ]]; then flags+=(-DCONFIG_GMK70_STAGING_QUALIFICATION=1); fi
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie \
        -Itests/zmk/receiver_stubs -Ifirmware/zmk/update -Ifirmware/zmk/wired -Ifirmware/qmk-common/keyboards/zuoya/gmk70/update "${flags[@]}" \
        tests/zmk/service_lifecycle_test.c firmware/zmk/update/{raw_hid,receiver}.c firmware/qmk-common/keyboards/zuoya/gmk70/update/{image,protocol}.c \
        -o "artifacts/tests/zmk/service-lifecycle-$profile"
    "artifacts/tests/zmk/service-lifecycle-$profile"
    "artifacts/tests/zmk/service-lifecycle-$profile" failure
    "artifacts/tests/zmk/service-lifecycle-$profile" missing
done

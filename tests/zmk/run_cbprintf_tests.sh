#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../scripts/project.sh"
ulimit -c 0
mkdir -p artifacts/tests/zmk
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -fno-pie -fsanitize=address,undefined
  -fno-sanitize-recover=all -fno-omit-frame-pointer -ffunction-sections -fdata-sections
  -DCONFIG_ARCH_POSIX=1 -DCONFIG_64BIT=1 -DCONFIG_CBPRINTF_NANO=1 -DCONFIG_CBPRINTF_LIBC_SUBSTS=1
  -Itests/zmk/cbprintf_stubs -isystem dependencies/zephyr/include)
for profile in full reduced; do
  profile_flags=(-DCONFIG_CBPRINTF_FULL_INTEGRAL=1)
  if [[ "$profile" == reduced ]]; then profile_flags=(-DCONFIG_CBPRINTF_REDUCED_INTEGRAL=1); fi
  cc "${flags[@]}" "${profile_flags[@]}" -c dependencies/zephyr/lib/os/cbprintf_nano.c \
    -o "artifacts/tests/zmk/cbprintf-nano-$profile.o"
  cc "${flags[@]}" "${profile_flags[@]}" -Dsnprintf=gmk_native_snprintf -Dsprintf=gmk_native_sprintf \
    -Dvsnprintf=gmk_native_vsnprintf -Dvsprintf=gmk_native_vsprintf \
    -c dependencies/zephyr/lib/libc/minimal/source/stdout/sprintf.c \
    -o "artifacts/tests/zmk/cbprintf-sprintf-$profile.o"
  cc "${flags[@]}" "${profile_flags[@]}" -c dependencies/zephyr/lib/os/cbprintf.c \
    -o "artifacts/tests/zmk/cbprintf-wrapper-$profile.o"
  cc "${flags[@]}" "${profile_flags[@]}" -Wconversion -Wsign-conversion -fanalyzer -no-pie -Wl,--gc-sections \
    tests/zmk/cbprintf_native_test.c "artifacts/tests/zmk/cbprintf-nano-$profile.o" \
    "artifacts/tests/zmk/cbprintf-sprintf-$profile.o" "artifacts/tests/zmk/cbprintf-wrapper-$profile.o" \
    -o "artifacts/tests/zmk/cbprintf-native-$profile"
  "artifacts/tests/zmk/cbprintf-native-$profile"
done

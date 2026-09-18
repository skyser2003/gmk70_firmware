#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
qmk_root="$(cd qmk_firmware && pwd -P)"
[[ -f "$qmk_root/builddefs/build_keyboard.mk" ]] || exit 1
for relative_path in .build keyboards/zuoya/gmk70 keyboards/linker/wireless; do
    clean_target="$(realpath -m "$qmk_root/$relative_path")"
    case "$clean_target" in
        "$qmk_root/"*) rm -rf -- "$clean_target" ;;
        *) printf 'Refusing to clean outside QMK: %s\n' "$clean_target" >&2; exit 1 ;;
    esac
done

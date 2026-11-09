#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
if [[ ! -f dependencies/source-files.json ]]; then
    git submodule update --init --depth 1 -- dependencies/qmk
    tag="$(python3 -c 'import json; print(json.load(open("dependencies/sources.lock.json"))["qmk"]["tag"])')"
    if ! git -C "$qmk_root" rev-parse --verify "refs/tags/$tag" >/dev/null 2>&1; then
        git -C "$qmk_root" fetch --depth 1 origin "refs/tags/$tag:refs/tags/$tag"
    fi
    git -C "$qmk_root" submodule update --init --depth 1 -- lib/chibios lib/chibios-contrib lib/printf lib/lufa
fi
if [[ -f dependencies/source-files.json && ! -f "$qmk_root/quantum/quantum.c" ]]; then
    # A Vial source distribution includes only its selected framework.
    python3 tools/build-support/project_build.py verify-vial
else
    python3 tools/build-support/project_build.py verify
fi
if [[ "$(uname -s)/$(uname -m)" != Linux/x86_64 ]]; then
    printf 'Automatic toolchain setup supports Linux x86_64 (including WSL). Install ARM GCC and uv manually on this host.\n' >&2
    exit 2
fi
[[ ! -L .work && ! -L .work/downloads && ! -L .work/toolchains ]] || exit 2
mkdir -p .work/downloads .work/toolchains
fetch_verified() {
    local url="$1" archive="$2" checksum="$3"
    if [[ ! -f "$archive" ]]; then
        curl --fail --location --retry 3 "$url" -o "$archive"
    fi
    printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check
}
uv_binary=".work/toolchains/uv-x86_64-unknown-linux-gnu/uv"
if [[ ! -x "$uv_binary" ]]; then
    fetch_verified 'https://github.com/astral-sh/uv/releases/download/0.8.22/uv-x86_64-unknown-linux-gnu.tar.gz' \
        .work/downloads/uv.tar.gz 741ff1f5742c5a4a25d2f829e8395355e43f7a5ae2ebc6368e9ae2df0efb69cf
    tar -xzf .work/downloads/uv.tar.gz -C .work/toolchains
fi
arm_directory=".work/toolchains/$arm_toolchain_name"
[[ ! -L "$arm_directory" ]] || { printf 'Redirected compiler directory: %s\n' "$arm_directory" >&2; exit 2; }
if [[ ! -x "$arm_directory/bin/arm-none-eabi-gcc" ]]; then
    arm_url="$(python3 -c 'import json, sys; print(json.load(open("dependencies/sources.lock.json"))[sys.argv[1]]["url"])' "$arm_toolchain_lock")"
    arm_sha="$(python3 -c 'import json, sys; print(json.load(open("dependencies/sources.lock.json"))[sys.argv[1]]["sha256"])' "$arm_toolchain_lock")"
    arm_archive=".work/downloads/${arm_url##*/}"
    fetch_verified "$arm_url" "$arm_archive" "$arm_sha"
    tar -xf "$arm_archive" -C .work/toolchains
fi
printf 'Selected compiler: %s\n' "$GMK70_TOOLCHAIN"
"$arm_directory/bin/arm-none-eabi-gcc" --version
"$uv_binary" sync --frozen --project tools/build-support
"$uv_binary" sync --frozen --project tools/keymap-migration
"$uv_binary" sync --frozen --project tools/battery-diagnostics
"$uv_binary" sync --frozen --project tools/firmware-update
printf '\nReady: make test, make build-default, or make build\n'

#!/usr/bin/env bash
# Shared paths and environment for commands launched from any directory.
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$project_root"
if [[ -f .env ]]; then
    set -a
    source .env
    set +a
elif [[ -f .envrc ]]; then
    source .envrc
fi
qmk_root="$project_root/dependencies/qmk"
qmk_common="$project_root/firmware/qmk-common"
build_tools="$project_root/tools/build-support/.venv/bin"
export GMK70_TOOLCHAIN="${GMK70_TOOLCHAIN:-arm}"
case "$GMK70_TOOLCHAIN" in
    arm)
        arm_toolchain_name=arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi
        arm_toolchain_lock=arm_toolchain ;;
    xpack)
        arm_toolchain_name=xpack-arm-none-eabi-gcc-14.2.1-1.1
        arm_toolchain_lock=xpack_arm_toolchain ;;
    *) printf 'Unsupported GMK70_TOOLCHAIN=%s; choose arm or xpack.\n' "$GMK70_TOOLCHAIN" >&2; return 2 ;;
esac
export UV_CACHE_DIR="$project_root/.work/uv-cache"
export XDG_CONFIG_HOME="$project_root/.work/config"
export PATH="$build_tools:$project_root/.work/toolchains/$arm_toolchain_name/bin:$project_root/.work/toolchains/uv-x86_64-unknown-linux-gnu:$PATH"

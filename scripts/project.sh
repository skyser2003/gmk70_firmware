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
export UV_CACHE_DIR="$project_root/.work/uv-cache"
export XDG_CONFIG_HOME="$project_root/.work/config"
export PATH="$build_tools:$project_root/.work/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin:$project_root/.work/toolchains/uv-x86_64-unknown-linux-gnu:$PATH"

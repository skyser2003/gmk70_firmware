#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
exec python3 tools/build-support/github_build.py "$@"

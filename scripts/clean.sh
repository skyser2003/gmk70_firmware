#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
python3 tools/build-support/project_build.py clean

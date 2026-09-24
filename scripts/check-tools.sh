#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
uv run --offline --frozen --project tools/build-support ruff check tools/build-support/project_build.py tests/repository
uv run --offline --frozen --project tools/build-support ruff format --check tools/build-support/project_build.py tests/repository
uv run --offline --frozen --project tools/build-support mypy --strict tools/build-support/project_build.py tests/repository
uv run --offline --frozen --project tools/build-support python -m unittest discover -s tests/repository -v
uv run --offline --frozen --project tools/keymap-migration ruff check tools/keymap-migration
uv run --offline --frozen --project tools/keymap-migration ruff format --check tools/keymap-migration
uv run --offline --frozen --project tools/keymap-migration mypy --strict tools/keymap-migration
uv run --offline --frozen --project tools/keymap-migration python -m unittest discover -s tools/keymap-migration/tests -v

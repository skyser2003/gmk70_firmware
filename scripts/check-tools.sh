#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/project.sh"
uv run --offline --frozen --project tools/build-support ruff check tools/build-support/*.py tests/repository
uv run --offline --frozen --project tools/build-support ruff format --check tools/build-support/*.py tests/repository
uv run --offline --frozen --project tools/build-support mypy --strict tools/build-support/*.py tests/repository
uv run --offline --frozen --project tools/build-support python -m unittest discover -s tests/repository -v
uv run --offline --frozen --project tools/keymap-migration ruff check tools/keymap-migration
uv run --offline --frozen --project tools/keymap-migration ruff format --check tools/keymap-migration
uv run --offline --frozen --project tools/keymap-migration mypy --strict tools/keymap-migration
uv run --offline --frozen --project tools/keymap-migration python -m unittest discover -s tools/keymap-migration/tests -v
uv run --offline --frozen --project tools/battery-diagnostics ruff check tools/battery-diagnostics
uv run --offline --frozen --project tools/battery-diagnostics ruff format --check tools/battery-diagnostics
uv run --offline --frozen --project tools/battery-diagnostics mypy --strict tools/battery-diagnostics
(
    cd tools/battery-diagnostics
    uv run --offline --frozen python -m unittest discover -s tests -v
)
(
    cd tools/firmware-update
    uv run --offline --frozen ruff check .
    uv run --offline --frozen ruff format --check .
    uv run --offline --frozen mypy --strict .
    uv run --offline --frozen python -m unittest discover -s tests -v
)

if [[ -x firmware/zmk/tools/.venv/bin/python ]]; then
    uv run --offline --frozen --project firmware/zmk/tools ruff check firmware/zmk/tools tests/zmk/test_*.py
    uv run --offline --frozen --project firmware/zmk/tools ruff format --check firmware/zmk/tools tests/zmk/test_*.py
    uv run --offline --frozen --project firmware/zmk/tools mypy --strict firmware/zmk/tools/*.py tests/zmk/test_*.py
    uv run --offline --frozen --project firmware/zmk/tools python -m unittest discover -s tests/zmk -p 'test_*.py' -v
fi

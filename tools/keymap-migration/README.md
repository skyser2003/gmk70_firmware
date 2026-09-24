# Offline legacy keymap migration

Use only for keymaps exported from this project's earlier build, which used `0x7E03` for 2.4 GHz and `0x7E05` for battery. Do not use on stock firmware exports or an already converted VIA export. Numeric collisions make automatic detection impossible.

From the repository root, with uv installed:

```sh
uv run --project tools/keymap-migration python tools/keymap-migration/keymap_migration.py --from legacy-port-v1 old-export.json stock-compatible-export.json
```

The new output must not already exist. The input remains unchanged. The converter supports saved VIA `layers` and `encoders`, or this project's raw `matrix_keycodes` backups. Hex strings, numeric codes, `USERnn`, and `QK_KB_n` are supported. The earlier unimplemented reserved slot is converted to `KC_NO`. Macros and metadata are preserved. The tool never accesses USB and does not flash or write EEPROM.

After initializing the new firmware's defaults, import a converted **VIA export** through VIA's Save/Load interface. A converted raw recovery dump still needs a dedicated matrix restore procedure; it is not a VIA import file. The original vendor keyboard definition is neither kind of saved keymap and is rejected.

Validation:

```sh
uv run --project tools/keymap-migration ruff check tools/keymap-migration
uv run --project tools/keymap-migration mypy --strict tools/keymap-migration
uv run --project tools/keymap-migration python -m unittest discover -s tools/keymap-migration/tests -v
```

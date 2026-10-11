# Firmware dependency provenance

The former ordinary-file QMK tree has been replaced by the official `qmk/qmk_firmware` submodule at `dependencies/qmk/`. Its release tag is **0.34.4**, commit `08c662f286ddfd12a985f57b584b02eca5af0ae6`. The framework is unmodified. Our board and radio sources live under `firmware/qmk-common/`; QMK/VIA additions live under `firmware/qmk-via/`.

[The lockfile](../dependencies/sources.lock.json) records the QMK commit and the release's own ChibiOS, ChibiOS-Contrib, LUFA and printf commits. `make setup` initializes only these required libraries. In particular, it does not recursively initialize the unused MCUX SDK or other platform libraries. Original source copyright notices and licenses stay with each dependency and are included in source exports.

The [historical manifest](provenance/BUNDLED-SOURCES-2024.tsv) preserves the exact versions formerly copied into this repository, including the old QMK commit `3d4da6de29c8635c9cd232ce456d8fec8d31921b`. It is historical evidence, not the active build lockfile. The [upstream comparison](QMK-UPSTREAM-COMPARISON.md) and [licensing audit](LICENSING.md) describe that old snapshot. Historical Git commits and previously distributed source archives retain their original contents.

## Builds and source exports

`make setup` explicitly provisions dependencies and locally installed compiler/Python tools. Subsequent builds verify the locked revisions, reject modified dependencies, and compile directly from the framework and maintained board/radio/keymap paths. Sources are neither copied nor symlinked. The external-source adapter rejects collisions and records each local file's origin. Generated headers and objects stay under `.work/<platform>/<variant>/<normal|updater>/`; dependency checkouts remain unmodified. See [external-source builds](EXTERNAL-BUILD.md).

Each artifact folder includes its exact dependency lockfile, source origins, checksums and complete `source.tar.gz`. Archives include working project files and prepared dependency contents, while excluding Git metadata, generated firmware, virtual environments and caches. `dependencies/source-files.json` in the archive permits dependency integrity checks without Git. Extracted archives can build with preinstalled tools and without source downloads. Compiler and Python distributions are provisioned separately.

`make clean` removes only generated QMK/Vial build directories. `make format` is limited to project-owned sources and tests.

## Updating dependencies

Choose an explicit upstream release tag, resolve its commit, and update the submodule gitlink and lockfile together. Update the selected library pins to match that release; never use `--remote` during a build or silently follow `master`. Run regressions and both firmware variants, verify firmware size and BIN/HEX agreement, then verify clean rebuilds and a standalone source export. Keep future Vial and ZMK sources in the folders described by [the architecture](FIRMWARE-ARCHITECTURE.md).

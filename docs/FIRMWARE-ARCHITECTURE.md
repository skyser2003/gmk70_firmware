# Firmware dependency and source layout

This repository owns GMK70 board integration for QMK/VIA, Vial and ZMK.
Framework dependencies stay unmodified and independently pinned.

## Dependency ownership

| Build | Upstream | Dependency form |
| --- | --- | --- |
| QMK/VIA | Official QMK `0.34.4`, `08c662f286ddfd12a985f57b584b02eca5af0ae6` | `dependencies/qmk/` Git submodule |
| Vial | `vial-kb/vial-qmk`, `dd43959ae5c08d8a28d38a1acf7b04e86b14a344` | `dependencies/vial-qmk/` Git submodule |
| ZMK | `zmkfirmware/zmk`, `4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85` | Explicit pinned checkout at `dependencies/zmk/` |
| Zephyr | ZMK fork, `10ba6d0cb38bc3d258775d27982f707599320085` | Explicit pinned checkout at `dependencies/zephyr/` |

[The root lockfile](../dependencies/sources.lock.json) records exact sources,
library pins and toolchains. ZMK's upstream manifests select matching Zephyr
and modules; `make setup-zmk` resolves no moving branches and verifies those
pins. Its ignored checkouts under `dependencies/` are initialized by
`firmware/zmk/tools/workspace.py`, rather than parent Git submodules.
See [ZMK provenance](../firmware/zmk/PROVENANCE.md).

QMK and Vial retain their own library pins. Never share a mutable ChibiOS
checkout or substitute official QMK for the Vial fork. Vial GUI versions are
separate from firmware revisions.

## Maintained sources and outputs

| Path | Ownership |
| --- | --- |
| `firmware/qmk-common/` | Shared board, keymap, radio, low-power, lighting and updater code |
| `firmware/qmk-via/` | Official-QMK configuration and compatibility adapters |
| `firmware/qmk-vial/` | Vial keymap, UID, unlock configuration, definition and adapters |
| `firmware/zmk/` | Native ZMK board/SoC, drivers, behaviors, split/radio, settings and updater |
| `definitions/via/` | Unchanged original vendor VIA definition |
| `scripts/`, `tools/` | Setup, build, packaging, migration and separate device clients |
| `tests/` | Firmware/tool regressions and required fixtures |
| `.work/<platform>/<variant>/` | Ignored generated headers, objects and build metadata |
| `artifacts/<platform>/<variant>/` | Generated firmware distributions and matching sources |

QMK and Vial compile shared maintained sources directly through the
[external-source adapter](EXTERNAL-BUILD.md). It records origins and rejects
collisions without copying/symlinking sources or editing dependencies.
Each platform, variant and normal/updater mode has isolated intermediates.

ZMK shares hardware facts and protocol contracts with the QMK ports; its
Zephyr implementation remains separate. Studio is a build variant of that
same integration, with USB CDC RPC and application-owned settings banks.
The universal image selects roles at runtime; LEFT owns saved settings.
Bluetooth typing through the external radio does not provide Bluetooth Studio.

## Build and dependency workflow

Use `make setup`, `make setup-vial` or `make setup-zmk` explicitly.
Normal builds fail for missing, dirty or incorrectly pinned dependencies and
never download sources. Load environment files through `scripts/project.sh`.

`make clean` removes generated intermediates while preserving maintained
sources, dependencies and published artifacts. Source exports include exact
dependency contents, licenses and integrity manifests for offline rebuilding.
Preserve [bundled provenance](BUNDLED-SOURCES.md) when updating dependencies.

Build/test commands never flash, reset or change the connected keyboard.
See [release instructions](RELEASING.md), [Vial firmware](VIAL-PORT.md) and
[ZMK builds](../firmware/zmk/README.md).

# GMK70 firmware

Firmware for the ZUOYA GMK70 split keyboard (WB32FQ95), supporting USB,
Bluetooth and 2.4 GHz.

## Base versions

- **QMK/VIA:** official [QMK 0.34.4](https://github.com/qmk/qmk_firmware/tree/0.34.4).
- **Vial:** [vial-qmk at `dd43959ae5c0`](https://github.com/vial-kb/vial-qmk/commit/dd43959ae5c08d8a28d38a1acf7b04e86b14a344).

Both upstream dependencies remain unmodified. Exact framework, library and
toolchain versions are pinned in [sources.lock.json](dependencies/sources.lock.json).
Project firmware sources live in `firmware/`.

Board/radio code is shared in `firmware/qmk-common/`. Framework and RGB
integration are selected separately from `firmware/qmk-via/` and
`firmware/qmk-vial/`.

## What this repository provides

- QMK/VIA release and debug builds, plus a fixed-keymap QMK build.
- Shared board/radio integration targeting stock V15 behavior, including stock
  default keymaps, custom keycodes, RGB lighting and five-minute default idle sleep.
- Five-millisecond key debounce in both VIA and Vial builds.
- Experimental Vial with keymap and knob remapping, macros, tap dances, combos,
  key overrides, live QMK settings and VialRGB. See [Vial features](docs/VIAL-PORT.md).
- Optional split updater: update both halves through the left half's USB connection.
- QMK/VIA updater EEPROM backups, QMK debug split/sleep diagnostics and
  debug-updater persistent sleep logs. Vial builds exclude backups and diagnostics.

Complete stock equivalence and hardware validation of the latest Vial/updater
changes remain unverified. ZMK is not implemented.

## Build

Use Linux x86_64 or WSL2 with Git, make, curl, tar, xz, a native C compiler and
Python 3.12 or newer. Setup installs the ARM compiler and Python build tools locally.

```sh
make setup        # pinned QMK sources and build tools
make setup-vial   # additionally prepare pinned Vial sources, if needed
```

Choose a build target:

| Command | Output |
| --- | --- |
| `make build` | QMK/VIA release and debug |
| `make build-release` | QMK/VIA release |
| `make build-debug` | QMK/VIA with diagnostics |
| `make build-default` | Fixed-keymap QMK without VIA |
| `make build-updater` | QMK/VIA release and debug with split updater |
| `make build-vial` | Normal Vial firmware |
| `make build-vial-updater` | Vial firmware with split updater |

Builds write firmware, checksums and matching source archives to
`artifacts/qmk/<variant>/` or `artifacts/vial/<variant>/`.
Builds read `dependencies/` and `firmware/` directly, without copying or symlinking
source trees. Generated headers and objects live under `.work/`; `make clean`
removes them. See [external-source builds](docs/EXTERNAL-BUILD.md).
Build and test commands do not flash or reset a keyboard.

```sh
make test         # firmware regression tests
make check-tools  # Python formatting, lint, type checks and tests
```

GitHub Actions checks all seven variants on `main`. Pushes to `release/via` and
`release/vial` build and publish only that family's firmware, with independent
`via-vX.X.X` and `vial-X.X.X` patch versions. PRs and manual runs provide build
artifacts. See [release instructions](docs/RELEASING.md).

## Using the firmware

Back up your configuration before updating, and install the same build on both
halves. Stock/VIA and Vial use different EEPROM layouts; independently rebuilt
Vial images reset dynamic mappings and macros.

- **VIA:** load the [original GMK70 definition](definitions/via/ZUOYA_GMK70.json)
  through the Design tab. See [compatibility and migration](docs/STOCK-COMPATIBILITY.md).
- **Vial:** the layout is embedded. Physical unlock uses **Escape + left Shift**.

## Split updater

Updater builds contain a small resident installer and the keyboard application.
Initially install `install.bin` or `install.hex` on each half separately over USB;
each half must have a verified 256 KB MCU.

For later updates, connect the left half by USB in wired mode and keep the halves
linked. The host tool uses `application.bin` and `update.json` from a compatible
updater package, updating and verifying the right half through the split cable
before updating the left. Incoming images are staged and checked before the
resident installer replaces the application. Vial updates require physical unlock.

See the [updater guide](docs/SPLIT-UPDATER.md) for compatibility requirements,
detailed flashing instructions and recovery behavior.

Additional details: [stock reconstruction](docs/STOCK-V15-RECONSTRUCTION.md),
[EEPROM backups](docs/EEPROM-QUERIES.md),
[persistent logs](docs/PERSISTENT-SLEEP-LOG.md), and
[source provenance and licenses](docs/BUNDLED-SOURCES.md).

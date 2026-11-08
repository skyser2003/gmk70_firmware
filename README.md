# GMK70 firmware

Firmware for the ZUOYA GMK70 split keyboard (WB32FQ95). QMK/Vial support USB,
Bluetooth and 2.4 GHz. Native ZMK supports joined split operation over USB and
the existing stock radio; see the [wireless scope](docs/ZMK-WIRELESS.md).

## Base versions

- **QMK/VIA:** official [QMK 0.34.4](https://github.com/qmk/qmk_firmware/tree/0.34.4).
- **ZMK:** [pinned ZMK/Zephyr](firmware/zmk/PROVENANCE.md), one universal Studio image, native updater and separate bounded staging targets.
- **Vial:** [vial-qmk at `dd43959ae5c0`](https://github.com/vial-kb/vial-qmk/commit/dd43959ae5c08d8a28d38a1acf7b04e86b14a344).

Upstream dependencies remain unmodified. Exact framework, library and
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
- Shared VIA/Vial idle interrupt scanning and physical pairing-hold guards,
  plus ZMK LED supply control. See [custom feature parity](docs/CUSTOM-FEATURE-PARITY.md).
- A [custom behavior checklist](docs/CUSTOM-BEHAVIOR-CHECKLIST.md) describes
  native integration and hardware limits across VIA, Vial and ZMK.
- Experimental Vial with keymap and knob remapping, macros, tap dances, combos,
  key overrides, live QMK settings and VialRGB. See [Vial features](docs/VIAL-PORT.md).
- Optional split updater: update both halves through the left half's USB connection.
- QMK/VIA updater EEPROM backups, QMK debug split/sleep diagnostics and
  debug-updater persistent sleep logs. Normal Vial builds exclude backups and diagnostics;
  a separate [radio diagnostic build](docs/RADIO-DIAGNOSTICS.md) provides RAM counters.

The [universal ZMK Studio application](docs/ZMK-UNIVERSAL.md) serves both halves,
selected by the PB9 role pin. LEFT owns the saved keymap and host services;
RIGHT supplies its keys and RGB. Four Windows/Mac layers, lighting, battery
actions, stock-radio selection and function/media flip are implemented. Studio
exposes 72 physical keys and two virtual knob-rotation positions.

ZMK includes an explicitly unlocked updater and no permanent hardware watchdog.
See the [updater workflow](docs/ZMK-PERMANENT-WIRED.md) for installation checks
and recovery limits. Complete stock equivalence remains unverified.

## Build

Use Linux x86_64 or WSL2 with Git, make, curl, tar, xz, a native C compiler, a
C++20-capable native `c++` compiler and Python 3.12 or newer. The C++ compiler
is used by the native USB/B1 regression. Setup installs the ARM compiler and
Python build tools locally.

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
| `make build-vial-radio-diagnostic` | Optional Vial updater with RAM radio capture |
| `make build-zmk-wired-studio-updater` | Universal Studio image, persistent settings and Feature updater |
| `make build-zmk-wired-updater` | Native permanent wired ZMK application/updater A |
| `make build-zmk-release` | Universal ZMK Studio release package and matching sources |
| `make build-zmk-wired-update-test` | Different native wired update-test image B |
| `make build-zmk-wired-qualification` | Bounded no-feed staging Q; trial packages only |
| `make build-zmk` | Experimental left ZMK staging trial; requires `make setup-zmk` |
| `make build-zmk-right-trial` | Experimental right ZMK trial with post-main reset proof |

Builds write firmware, checksums and matching source archives to
`artifacts/qmk/<variant>/` or `artifacts/vial/<variant>/`. ZMK outputs are under
`artifacts/zmk/{wired-studio-updater,wired-updater,wired-update-test,wired-qualification,left-trial,right-trial}/`;
see the [build instructions](firmware/zmk/README.md). Native A/B packages use
`profile=zmk`, require the exact existing resident and do not emit a full initial
installer. The universal Studio package uses `profile=zmk-studio-universal`,
with a 104KiB application limit and separate 16KiB settings reservation.
Qualification Q emits target-specific staging packages only.
Builds read `dependencies/` and `firmware/` directly, without copying or symlinking
source trees. Generated headers and objects live under `.work/`; `make clean`
removes them. See [external-source builds](docs/EXTERNAL-BUILD.md).
Build and test commands do not flash or reset a keyboard.

```sh
make test         # firmware regression tests
make check-tools  # Python formatting, lint, type checks and tests
```

GitHub Actions runs general checks and firmware builds only on pushes to
`release/via`, `release/vial` or `release/zmk`. Each branch publishes its own family,
with independent `via-vX.X.X`, `vial-X.X.X` and `zmk-vX.X.X` patch versions. ZMK
publishes the universal Studio/updater image; trials and diagnostic images are
excluded. Pushes to `main` or other branches, PRs, tags and manual events do not
start the workflow. See [release instructions](docs/RELEASING.md).

## Using the firmware

For native ZMK, see the [Studio workflow](docs/ZMK-STUDIO.md) and the
[wired updater workflow](docs/ZMK-PERMANENT-WIRED.md).
Transfer and installation verification are separate; never replay INSTALL after a
restart-verification timeout.

For QMK/Vial and the universal ZMK Studio package, back up your configuration
before updating and use the same application build on both halves. Experimental
staging trials use a [separate launcher](docs/ZMK-TRIAL-LAUNCHER.md).
Stock/VIA and Vial use different EEPROM layouts; independently rebuilt
Vial images reset dynamic mappings and macros.

- **VIA:** load the [original GMK70 definition](definitions/via/ZUOYA_GMK70.json)
  through the Design tab. See [compatibility and migration](docs/STOCK-COMPATIBILITY.md).
- **Vial:** the layout is embedded. Physical unlock uses **Escape + left Shift**.

## Split updater

Updater builds contain a small resident installer and the keyboard application.
Initially install `install.bin` or `install.hex` on each half separately over USB;
each half must have a verified 256 KB MCU.

For later QMK/Vial updates, connect the left half by USB in wired mode and keep the halves
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

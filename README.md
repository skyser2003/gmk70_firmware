# GMK70 firmware

Custom firmware for the **ZUOYA GMK70** split keyboard:

- **QMK/VIA:** stock-oriented behavior with VIA configuration.
- **QMK/Vial:** Vial configuration, advanced key features and readable GMK70 controls.
- **ZMK:** native ZMK firmware with Studio configuration and one application for both halves.

All three use the original radio for Bluetooth and 2.4 GHz. The halves still
need their bridge cable; configuration and firmware updates use USB.

## Changes from stock

The QMK builds retain the stock custom-key numbering, Windows/Mac layouts and
five-minute default idle sleep. Board-specific additions and integration fixes include:

- **Battery indicators with RGB off:** holding Show Battery or Show Charging
  temporarily lights the indicators. Releasing the key restores darkness without
  changing the saved RGB setting.
- **Right-half RGB recovery (VIA/Vial):** LED power follows the synchronized
  lighting setting, including when the right half starts with its LEDs disabled.
- **Wake fixes (VIA/Vial):** delayed wake initialization preserves held modifiers,
  and a local right-half wireless wake signals the left half to wake too.
- **Key-release handling:** function/media shortcuts release the output selected
  when pressed, even after an OS or function-mode change. Switching hosts clears
  outgoing key reports; releasing or remapping a pairing key cancels pending pairing.
- **Battery-reading validation:** stale or invalid percentages appear as unknown
  instead of being used for a low-battery warning or shutdown.
- **GMK70 controls:** readable names for board actions in Vial, and two extra
  Studio key positions for remapping knob rotation without modifying Studio.
- **Split updater:** update both halves through left USB after initial setup.
- **Optional VIA tools:** updater EEPROM backups, debug diagnostics and
  debug-updater persistent sleep logs. Normal Vial builds exclude these tools.

Current ZMK has fewer advanced key behaviors and simpler lighting than Vial.
Complete stock equivalence and battery-life improvements are not established.
See the [feature comparison and known issues](docs/CUSTOM-FEATURE-PARITY.md).

## Downloads

Open the newest release in the matching list and download its assets:

| Firmware | Release page | Packages |
| --- | --- | --- |
| QMK/VIA | [VIA releases](https://github.com/skyser2003/gmk70_firmware/releases?q=tag%3Avia-v&expanded=true) | Normal, debug and updater builds |
| QMK/Vial | [Vial releases](https://github.com/skyser2003/gmk70_firmware/releases?q=tag%3Avial-&expanded=true) | Normal, secure updater and unlocked updater builds |
| ZMK Studio | [ZMK releases](https://github.com/skyser2003/gmk70_firmware/releases?q=tag%3Azmk-v&expanded=true) | Universal Studio/updater build |

These links show each family's releases newest first. Each release includes
checksums and matching sources. Features described here follow `main`; published
packages may not include every unreleased change.

ZMK packages built from current `main` include first-install images and later
application updates. Older releases may contain application updates only.
Read the [updater instructions](#updating-firmware) before choosing a file.

New builds of all three families provide **BIN firmware only**. BIN files do not contain
flash addresses; choose the file and address shown below. Older releases may
also include HEX files.

## Configuration

- **VIA:** enable the Design tab and load the
  [original GMK70 JSON](definitions/via/ZUOYA_GMK70.json).
- **Vial:** connect over USB; the layout loads from firmware. Named board
  actions are in the User tab. Secure builds use **Escape + left Shift** when
  Vial requests physical unlock. See [Vial controls](docs/VIAL-MANUAL-KEYS.md).
- **ZMK Studio:** join the halves, select wired mode and connect left USB.
  Select the keyboard's serial port in Studio, edit bindings and click Save.
  See [Studio setup](docs/ZMK-STUDIO.md) and [knob remapping](docs/ZMK-KNOB-KEYS.md).

Back up settings before updating. Independently rebuilt Vial images can reset
mappings and macros; stock/VIA, Vial and ZMK settings are not interchangeable.
ZMK and the separately named Vial `unlocked-updater` omit physical unlock;
normal Vial/updater packages retain it.

For source-level configuration:

| Area | Files |
| --- | --- |
| Shared QMK/VIA/Vial board settings and default map | [config.h](firmware/qmk-common/keyboards/zuoya/gmk70/config.h), [stock_keymap.h](firmware/qmk-common/keyboards/zuoya/gmk70/keymaps/stock_keymap.h) |
| Vial features, capacities and labels | [Vial keymap directory](firmware/qmk-vial/keymaps/vial/) |
| ZMK defaults and Studio options | [keymap](firmware/zmk/boards/zuoya/gmk70_wired/gmk70_wired.keymap), [studio.conf](firmware/zmk/boards/zuoya/gmk70_wired/studio.conf) |

## Build

Use Linux x86_64 or WSL2 with Git, make, curl, tar, xz, a C compiler, a
C++20 compiler and Python 3.12+. From the repository root:

```sh
source scripts/project.sh
make setup        # QMK sources, ARM compiler and Python tools
make setup-vial   # additionally required for Vial
make setup-zmk    # additionally required for ZMK
```

Choose the corresponding build:

| Command | Output |
| --- | --- |
| `make build` | VIA release and debug firmware |
| `make build-updater` | VIA release and debug updater packages |
| `make build-vial` | Normal Vial firmware |
| `make build-vial-updater` | Vial with secure split updater |
| `make build-vial-unlocked-updater` | Vial with permanently unlocked updater |
| `make build-updater-debug && make build-zmk-release` | Reviewed resident prerequisite, then universal ZMK Studio package |

Outputs are under `artifacts/qmk/`, `artifacts/vial/` and
`artifacts/zmk/wired-studio-updater/`. Setup initializes pinned dependencies;
builds use them directly without copying or symlinking source trees.
See [build architecture](docs/EXTERNAL-BUILD.md) and
[additional ZMK targets](firmware/zmk/README.md).

```sh
make test check-tools   # firmware regressions and Python tooling checks
make test-zmk           # additional ZMK tests after ZMK setup
```

Build and test commands do not flash or reset the keyboard.

## Updating firmware

Initial VIA/Vial updater and ZMK installation uses `install.bin`
on each half separately and requires verified 256 KB MCUs. These images contain
the resident and application; a full ZMK installation clears saved ZMK settings.
Later updates use `application.bin` and `update.json` through left USB in wired mode, with the
halves linked. See the [split-updater guide](docs/SPLIT-UPDATER.md).

| BIN file | Purpose | Flash address |
| --- | --- | --- |
| Normal VIA/Vial firmware | Direct installation without the split updater | `0x08000000` |
| `install.bin` | First installation with the resident updater | `0x08000000` |
| `application.bin` + `update.json` | Later updates through the resident updater | `0x08002000`, handled by the updater |

Do not flash `application.bin` at the start of flash. `resident.bin` is included
for package validation; it is not the complete keyboard firmware.

ZMK uses the same resident installer but a different inter-half updater transport.
Moving between QMK/Vial and canonical ZMK through the split updater requires the
[migration bridge and documented sequence](docs/ZMK-NATIVE-UPDATER-WIRE.md).
Direct first installation on both halves does not need that bridge.
Permanent ZMK has no hardware watchdog or automatic old-image rollback; see
[installation and recovery](docs/ZMK-PERMANENT-WIRED.md).

## Versions and releases

- **QMK/VIA:** [QMK 0.34.4](https://github.com/qmk/qmk_firmware/tree/0.34.4).
- **Vial:** [vial-qmk `dd43959ae5c0`](https://github.com/vial-kb/vial-qmk/commit/dd43959ae5c08d8a28d38a1acf7b04e86b14a344).
- **ZMK/Zephyr:** [pinned revisions and provenance](firmware/zmk/PROVENANCE.md).

Upstream dependencies remain unmodified. Exact versions are in
[sources.lock.json](dependencies/sources.lock.json); licenses and source origins
are documented in [BUNDLED-SOURCES.md](docs/BUNDLED-SOURCES.md).

GitHub Actions runs checks, builds and publication only on pushes to
`release/via`, `release/vial` or `release/zmk`, with separate automatic patch
versions. Ordinary `main` pushes do not run Actions.
See [release automation](docs/RELEASING.md).

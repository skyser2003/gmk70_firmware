# Firmware dependency and source layout

Status: QMK migration implemented, 2026-09-24. Vial and ZMK/Studio remain future ports.
This document incorporates the requested official-QMK submodule, latest QMK
release tag, future Vial fork, and future ZMK/Studio port. It supersedes the
old-revision recommendation in the earlier [upstream comparison](QMK-UPSTREAM-COMPARISON.md).

## Dependency ownership and versions

| Build | Upstream source | Owner of dependency revisions | Selected version |
| --- | --- | --- | --- |
| QMK default / VIA | `qmk/qmk_firmware` | Git submodule at `dependencies/qmk` | `0.34.4`, commit `08c662f286ddfd12a985f57b584b02eca5af0ae6` |
| Vial | `vial-kb/vial-qmk` | Separate Git submodule at `dependencies/vial-qmk` | Select and validate when implementing the port; no firmware tags were advertised when checked |
| ZMK default / Studio | `zmkfirmware/zmk`, its Zephyr version and modules | West manifest and frozen revisions under `firmware/zmk` | Latest tagged release observed: `v0.3.0`, commit `edf5c0814fd3ea202e43aad2d68fd32e882a518c`; recheck when implementing the port |

QMK's latest numeric tag was checked directly using `git ls-remote --tags --refs
--sort=-version:refname https://github.com/qmk/qmk_firmware.git 'refs/tags/0.*'`.
The selected tag was separately resolved, including a check for an annotated
tag's peeled commit. [QMK's tags page](https://github.com/qmk/qmk_firmware/tags)
also lists `0.34.4`. GitHub's Releases API is not the source of truth here: QMK
publishes version tags without corresponding GitHub Release entries.

The submodule gitlink must record the exact commit above, with the release tag
recorded in `dependencies/sources.lock.json` for provenance. Do not configure a
tracking branch or resolve "latest" during builds. A deliberate update chooses
a new release, resolves its commit, changes the pin and metadata together, and
reruns validation. CI verifies that the checkout and metadata agree.

The official [Vial firmware repository](https://github.com/vial-kb/vial-qmk)
currently advertises no Git tags (`git ls-remote --tags --refs` returned none).
Its GUI application's release number must not be treated as a firmware tag.
When that port begins, use a suitable firmware release tag if available;
otherwise record a tested exact Vial commit. Never silently follow the `vial`
branch or substitute official QMK for Vial.

ZMK uses [West](https://zmk.dev/docs/development/local-toolchain/setup/native)
to obtain its matching Zephyr and module revisions. Keep it exclusively under
West ownership rather than also registering the same checkouts as parent Git
submodules. The authored manifest records the chosen release; a committed frozen
manifest records the resolved commits for the entire dependency graph. Builds
use those frozen revisions. [West supports generating a frozen manifest](https://docs.zephyrproject.org/latest/develop/west/manifest.html#freezing-manifests).

QMK and Vial each retain their own upstream library pins. Do not share a mutable
ChibiOS checkout or make both forks use one arbitrarily chosen library version.
Pin compiler/container versions and Python environments per platform as needed.

## Directory structure

Directories marked future are created with the corresponding implementation,
not populated with placeholder firmware.

```text
dependencies/
  qmk/                         official QMK submodule
  vial-qmk/                    independent Vial submodule (future)
  sources.lock.json            upstream URLs, release labels, exact commits

firmware/
  qmk-common/                  our shared QMK-family code; ordinary tracked files
    keyboards/zuoya/gmk70/     board, matrix, LEDs, stock ABI, split and radio hooks
      keymaps/
        stock_keymap.h        shared compiled layout
        default/              shared baseline keymap
    drivers/wireless/          existing UART radio/host/low-power implementation
  qmk-via/
    keymaps/via/               QMK/VIA-specific keymap
    compat/                    adaptations for the selected official QMK release
  qmk-vial/
    keymaps/vial/              keymap.c, rules.mk, config.h, vial.json (future)
    compat/                    adaptations for the selected Vial fork (future)
  zmk/                         local Zephyr/ZMK module and user config (future)
    west.yml                   authored dependency manifest
    west.lock.yml              frozen dependency graph used for builds
    zephyr/module.yml          module registration
    boards/zuoya/              GMK70 board definitions and half variants
    soc/                       WB32 support if required
    dts/                       bindings and physical layout descriptions
    drivers/                   Zephyr-native peripheral and radio integration
    src/                       ZMK integration and behaviors
    config/                    keymaps and default/Studio build configurations
    CMakeLists.txt
    Kconfig

definitions/via/               unchanged original vendor definition
docs/                          shared hardware/protocol evidence and port notes
scripts/                       explicit dependency setup, composition, build, clean
tests/
  qmk/                         shared QMK-family regressions
  vial/                        Vial-specific tests (future)
  zmk/                         Zephyr/ZMK tests (future)
  repository/                  dependency, composition and clean checks
tools/                         project tooling, including offline keymap migration

.work/                         ignored, generated workspaces
  qmk/<variant>/               assembled QMK build tree per variant
  vial/<variant>/              assembled Vial build tree per variant
  zmk/                        West workspace, dependency checkouts and build dirs
artifacts/
  qmk/{default,via}/
  vial/vial/                   future
  zmk/{default,studio}/        future; separate half outputs below each variant
```

Keep one authoritative board/radio implementation in `qmk-common`. QMK and Vial
consume it through an explicit composition step into separate generated trees.
Only platform-specific keymaps and small compatibility adapters differ. Do not
maintain two complete copies of `gmk70.c` or the radio driver. Shared code should
call a small compatibility interface where the selected fork APIs differ.

The composition step records source origins, rejects unapproved path collisions,
and never edits the upstream submodules. Generated copies are disposable and
never an editing target. Each variant has its own intermediate directory so
QMK/Vial versions and build flags cannot contaminate another build. The
[Vial porting guide](https://get.vial.today/docs/porting-to-vial.html) explicitly
requires a separate fork checkout; sibling dependencies satisfy this requirement.

`vial.json` has one maintained home inside `firmware/qmk-vial/keymaps/vial/`, along
with the board UID and unlock configuration. Do not create another copy under
`definitions/`. The vendor VIA JSON stays byte-for-byte unchanged. Its custom
keycode contract remains explicit in shared firmware code.

ZMK shares hardware facts, matrix positions, LED locations and radio protocol
documentation with the QMK-family ports. QMK and Zephyr implementation files
remain separate. The local ZMK module stays in this repository; workspace setup
passes its module/config paths to the build without creating another maintained
GMK70 repository. Any generated manifest/config copy in `.work/zmk` derives from
the tracked files and is never maintained separately.

## Studio is a ZMK build variant

Use `firmware/zmk/config/` to select default versus Studio settings. Both builds
use the same ZMK revision, board, drivers, physical layouts and base keymap.
A separate ZMK Studio firmware fork or UI-source checkout is not required.

The [Studio integration requirements](https://zmk.dev/docs/features/studio#adding-zmk-studio-support-to-a-keyboard)
include a physical layout with key positions, settings storage, a supported RPC
transport and sufficient memory. For USB, the documented build enables
`CONFIG_ZMK_STUDIO` and the `studio-rpc-usb-uart` snippet, with unlock behavior in
the keymap. Enable Studio on the central half of a split and explicitly identify
each half in build and artifact names.

The current WB32FQ95 MCU and external vendor radio still require hardware/SoC,
USB, split and power integration for ZMK. Bluetooth typing through the existing
radio does not establish that ZMK Studio's BLE transport can run through it.
Validate USB Studio first once a basic ZMK port works; evaluate BLE separately.
The source layout makes room for that work without claiming the port is ready.

## Build and dependency workflow

- Provide explicit dependency setup/update commands, separate from compilation.
  Initialize only the selected platform and its required libraries. Normal
  builds fail clearly if a dependency is missing or has the wrong revision.
- Keep the existing QMK default and VIA entry points. Add actual Vial/ZMK entry
  points only as their ports become implemented; until then retain clear errors.
- Load project configuration through `scripts/project.sh` and pass upstream and
  output paths explicitly. Avoid dependence on a global `QMK_HOME` or unrelated
  West workspace.
- `make clean` removes generated build/intermediate data, preserving maintained
  sources, upstream checkouts and published artifacts. Dependency removal is a
  distinct operation. Reject redirected/symlinked cleanup paths as today.
- CI checks the requested dependency pins, composes fresh trees, runs relevant
  tests, and builds each supported variant independently. Upstream trees must
  remain unmodified after builds.
- Artifacts retain checksums, build logs, exact source revisions, compiler flags,
  licenses and a complete source export. The export includes required dependency
  contents so a source-archive build does not depend on Git metadata or downloads.
  Preserve the old bundled manifest as historical provenance; do not relabel the
  existing bundled files as if they already came from `0.34.4`.
- Firmware build and test commands never flash, reset, or change a keyboard.

## Migration sequence and acceptance

1. Preserve source identities and establish current default/VIA build baselines.
2. Move our board/radio code to `qmk-common` and place platform additions as above.
   During migration, update `AGENTS.md`, scripts, CI, test paths, packaging and
   documentation together. The requested dependency policy replaces the old
   no-submodules rule.
3. Add official QMK as a submodule at release `0.34.4` and its exact commit. Use
   this release's own library pins; the old library manifest is not a lockfile
   for the new release.
4. Adapt the local integration to the new release, keeping upstream unmodified.
   Check schema/build API changes, keycode values, VIA protocol and lighting
   translation, RGB hooks/effect numbering, host/radio routing, WB32 peripherals,
   split role selection, EEPROM allocation and bootloader integration.
5. Run the host regressions and compile default/VIA. Check binary sizes and the
   BIN/HEX payload agreement within each build. Do not demand identical firmware
   bytes across QMK versions: this is a framework upgrade as well as a dependency
   layout change. Verify reproducibility for repeated builds of the new pin.
6. Test clean/rebuild, dependency revision rejection, source export builds, and
   concurrent platform/variant isolation. Physical validation remains separate.
7. Add and validate the independent Vial pin and port later. Then implement the
   ZMK hardware port; Studio follows as a tested variant of that port.

The QMK migration is implemented by `scripts/setup.sh`, `scripts/build.sh` and `tools/build-support/project_build.py`. Current compatibility work and validation are recorded in [the migration notes](QMK-0.34-MIGRATION.md). Vial and ZMK/Studio remain separate future implementations.

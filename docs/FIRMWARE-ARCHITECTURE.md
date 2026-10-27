# Firmware dependency and source layout

Status, 2026-10-27: QMK migration and experimental Vial port implemented; an
experimental [left USB](ZMK-LEFT-FIRST-TRIAL.md) and
[right main-return](ZMK-RIGHT-FIRST-TRIAL.md) ZMK trials have executed separately
and returned to Vial. ZMK split/radio and Studio remain future work. See also the
[Vial port report](VIAL-PORT.md). This document records the separately pinned
frameworks and their maintained board sources. It supersedes the
old-revision recommendation in the earlier [upstream comparison](QMK-UPSTREAM-COMPARISON.md).

## Dependency ownership and versions

| Build | Upstream source | Owner of dependency revisions | Selected version |
| --- | --- | --- | --- |
| QMK default / VIA | `qmk/qmk_firmware` | Git submodule at `dependencies/qmk` | `0.34.4`, commit `08c662f286ddfd12a985f57b584b02eca5af0ae6` |
| Vial | `vial-kb/vial-qmk` | Separate Git submodule at `dependencies/vial-qmk` | `dd43959ae5c08d8a28d38a1acf7b04e86b14a344`; its own library pins; no firmware tags advertised |
| ZMK staging trials | `zmkfirmware/zmk`, its Zephyr version and modules | Exact commits in `dependencies/sources.lock.json`, verified against upstream manifests | ZMK `4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85`; Zephyr `10ba6d0cb38bc3d258775d27982f707599320085` |

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
The initial port records the exact tested 2026-07-26 snapshot in the lockfile.
If a suitable firmware release tag becomes available, select it deliberately. Never silently follow the `vial`
branch or substitute official QMK for Vial.

ZMK's upstream West manifests select matching Zephyr and modules. The current
left trial records exact resolved commits in the root lockfile and verifies them
against those manifests. `make setup-zmk` explicitly creates clean, pinned
checkouts under `dependencies/zmk`, `dependencies/zephyr` and
`dependencies/modules`; they are ignored dependencies, not maintained board
repositories or duplicate parent submodules. `make build-zmk` verifies and uses
those checkouts without downloads. See [ZMK provenance](../firmware/zmk/PROVENANCE.md).

QMK and Vial each retain their own upstream library pins. Do not share a mutable
ChibiOS checkout or make both forks use one arbitrarily chosen library version.
Pin compiler/container versions and Python environments per platform as needed.

## Directory structure

Directories marked future are created with the corresponding implementation,
not populated with placeholder firmware.

```text
dependencies/
  qmk/                         official QMK submodule
  vial-qmk/                    independent pinned Vial submodule
  zmk/                         unmodified pinned ZMK checkout
  zephyr/                      matching unmodified Zephyr checkout
  modules/                     pinned CMSIS/LVGL checkouts
  sources.lock.json            upstream URLs, release labels, exact commits

firmware/
  qmk-common/                  our shared QMK-family code; ordinary tracked files
    keyboards/zuoya/gmk70/     board, matrix, LEDs, stock ABI, split and radio hooks
      keymaps/
        stock_keymap.h        shared compiled layout
        default/              shared baseline keymap
    drivers/wireless/          existing UART radio/host/low-power implementation
  qmk-via/
    keymaps/release/           normal QMK/VIA configuration
    keymaps/debug/             same layout plus logging and USB bootloader command
    compat/                    adaptations for the selected official QMK release
  qmk-vial/
    keymaps/vial/              keymap.c, rules.mk, config.h, vial.json
    compat/                    adaptations for the selected Vial fork
  zmk/                         maintained local Zephyr/ZMK module
    zephyr/module.yml          module registration and roots
    boards/zuoya/gmk70_left/   left board, defconfig and keymap
    boards/zuoya/gmk70_right/  right board, defconfig and keymap
    soc/westberry/wb32fq95/    WB32 clocks and USB electrical integration
    dts/                       SoC and peripheral bindings
    drivers/                   native GPIO and USB device-controller drivers
    tools/                     pinned build tools, source/image verification
    trial/                     main-return adapter and separate bare fixtures
    CMakeLists.txt
    Kconfig

definitions/via/               unchanged original vendor definition
docs/                          shared hardware/protocol evidence and port notes
scripts/                       explicit dependency setup, external build, clean
tests/
  qmk/                         shared QMK-family regressions
  vial/                        Vial-specific tests (future)
  zmk/                         native USB and cold-start clock tests
  repository/                  dependency, external-source and clean checks
tools/                         project tooling, including offline keymap migration

.work/                         ignored, generated workspaces
  qmk/<variant>/<mode>/        generated QMK headers and objects per build mode
  vial/<variant>/<mode>/       generated Vial headers and objects per build mode
  zmk/{left-trial,right-trial}/normal/  generated Zephyr/ZMK build files
artifacts/
  qmk/{release,debug}/         the two VIA distributions
  qmk/default/                 optional fixed-keymap development baseline
  vial/vial/                   experimental normal Vial distribution
  zmk/{left-trial,right-trial}/ actual ZMK trials and build metadata
  zmk/trial-fixtures/          separately labeled bare return/hang programs
```

Keep one authoritative board/radio implementation in `qmk-common`. QMK and Vial
compile it directly through the [external-source adapter](EXTERNAL-BUILD.md).
Only platform-specific keymaps and small compatibility adapters differ. Do not
maintain two complete copies of `gmk70.c` or the radio driver. Shared code should
call a small compatibility interface where the selected fork APIs differ.

Build preparation records source origins, rejects source collisions,
and never edits the upstream submodules. Sources are neither copied nor
symlinked. Generated headers and objects are disposable and never an editing
target. Each variant and normal/updater mode has its own intermediate directory
so QMK/Vial versions and build flags cannot contaminate another build. The
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
- CI checks the requested dependency pins, prepares output directories, runs relevant
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

The QMK migration is implemented by `scripts/setup.sh`, `scripts/build.sh` and
`tools/build-support/project_build.py`. Current compatibility work and validation
are recorded in [the migration notes](QMK-0.34-MIGRATION.md). The experimental
Vial port is described in [its port report](VIAL-PORT.md). The
[left ZMK trial](ZMK-LEFT-FIRST-TRIAL.md) has USB execution evidence; the separate
[right trial](ZMK-RIGHT-FIRST-TRIAL.md) has post-main software reset and continuous
recovery evidence through left USB. Split/radio and Studio remain future work.

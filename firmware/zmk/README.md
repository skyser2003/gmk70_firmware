# GMK70 ZMK firmware

The canonical `wired-studio-updater` is one universal application for both
halves. PB9 selects the physical role: LEFT owns Studio settings and host
services; RIGHT scans keys and displays the central's RGB frames over the
joined split link. The stock radio provides Bluetooth and 2.4 GHz host transport.
See [wireless scope](../../docs/ZMK-WIRELESS.md).

Maintained sources live here. ZMK, Zephyr and their modules are unmodified,
ignored checkouts under `dependencies/`, initialized explicitly from
[the dependency lockfile](../../dependencies/sources.lock.json).
[PROVENANCE.md](PROVENANCE.md) identifies upstream sources and licenses.

## Setup and builds

Load the root environment through `scripts/project.sh`. The build scripts do
this themselves. Linux x86_64 or WSL2 needs a native C compiler and a C++20
compiler for host regressions.

```sh
make setup
make setup-zmk
make build-zmk-wired-studio-updater
make test test-zmk check-tools
```

Setup fetches exact locked revisions and installs pinned tools. Builds require
clean initialized dependencies and perform no source downloads. Build, package
and test commands never access the keyboard.

| Target | Purpose |
| --- | --- |
| `build-zmk-wired-studio-updater` | Universal Studio application and unlocked updater |
| `build-zmk-release` | Universal Studio portable package and matching sources |
| `build-zmk-wired-studio-update-test` | Distinct Studio image for update checks |
| `build-zmk-wired-studio-native-bridge` | Separate legacy/native migration bridge |
| `build-zmk-wired-studio-radio-qualification` | Separate radio qualification image |
| `build-zmk-wired-peripheral` | Smaller compatibility peripheral |
| `build-zmk-wired-updater` / `build-zmk-wired-update-test` | Static-keymap permanent A/B images |
| `build-zmk-wired-qualification` | Bounded staging image; no permanent INSTALL |
| `build-zmk` / `build-zmk-right-trial` | Experimental LEFT/RIGHT staging trials |
| `build-zmk-trial-fixtures` | Bare return/hang fixtures for the separate launcher |

Generated files live under `.work/zmk/<variant>/`; packages live under
`artifacts/zmk/<variant>/`. Legacy handoff/watchdog-rollback target names
resolve to canonical no-watchdog builds.

## Application and updater boundaries

Permanent applications link at `0x08002000`. Studio reserves 104 KiB for the
application and two 8 KiB settings banks at `0x0801c000..0x0801ffff`.
Packages use the reviewed 8 KiB resident: padded SHA256
`be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`,
CRC32 `1770733249`. Packaging reads the reviewed
`artifacts/qmk/debug-updater/resident.bin`; ZMK does not rebuild it. The canonical
universal build emits `install.bin` at `0x08000000` containing
the resident, application and erased settings/staging space for first installation
on each verified 256 KiB half. Application updates retain the existing resident
and settings. Development/bridge variants remain application-only. See
[first installation](../../docs/ZMK-PERMANENT-WIRED.md#first-installation).

The universal package uses `profile=zmk-studio-universal`; static A/B packages
use `profile=zmk`. Permanent firmware never initializes or feeds a hardware
watchdog. Update/settings writes retain service, USB, wired, role and storage
gates. Physical power cycling is the fallback for an unreachable application;
there is no old-image rollback slot.

Staging trials link at `0x08020000` and retain the separate launcher's short
watchdog without feeding it. LEFT trials expose a minimal USB keyboard;
RIGHT trials request reset only after native main returns and the matrix is
ready. Trial packages cannot be passed to ordinary INSTALL.

Use the [Studio guide](../../docs/ZMK-STUDIO.md),
[universal image contract](../../docs/ZMK-UNIVERSAL.md),
[updater workflow](../../docs/ZMK-PERMANENT-WIRED.md) and
[trial launcher guide](../../docs/ZMK-TRIAL-LAUNCHER.md) for device operations.

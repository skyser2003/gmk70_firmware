# GMK70 firmware

This repository maintains the GMK70 board and radio integration. It builds against the unmodified official [QMK release tag 0.34.4](https://github.com/qmk/qmk_firmware/tree/0.34.4), pinned as a Git submodule. The folder structure also reserves independent Vial and ZMK/Studio ports; see [the architecture](docs/FIRMWARE-ARCHITECTURE.md).

## Sources

| Folder | Purpose |
| --- | --- |
| `dependencies/qmk/` | Official QMK submodule, including its pinned libraries |
| `dependencies/sources.lock.json` | Release tag, exact commits, ARM toolchain version and checksum |
| `firmware/qmk-common/` | Authoritative GMK70 board, default layout, radio and power code |
| `firmware/qmk-via/` | VIA keymap and official-QMK/VIA compatibility additions |
| `firmware/qmk-vial/` | Future Vial integration using its own QMK fork |
| `firmware/zmk/` | Future ZMK hardware port and default/Studio configurations |
| `definitions/via/` | Unchanged original ZUOYA VIA definition |
| `tools/`, `tests/` | Build support, offline keymap migration and regression tests |
| `.work/qmk/<variant>/` | Generated build trees; edit `firmware/` instead |
| `artifacts/<platform>/<variant>/` | Generated firmware, build logs and matching sources |

Only the GMK70 integration is maintained here. Dependencies retain their source licenses and notices. [Source provenance](docs/BUNDLED-SOURCES.md) records the migration from the former bundled tree. The earlier licensing audit remains available with its historical scope identified.

## Build

An opt-in [left-USB split updater](docs/SPLIT-UPDATER.md) is available through
`make build-updater`. It requires a one-time direct USB installation on each
256 KB half and remains experimental pending hardware validation. Normal
`make build` packages retain the existing memory layout and behavior.

Updater builds also support [read-only EEPROM backups](docs/EEPROM-QUERIES.md)
from either half through left USB. After installing the query-enabled applications
on both halves, run `bash scripts/read-eeprom.sh --target both --output both-eeprom.json`
to save mappings, encoder assignments, macro bytes and stored settings.

Use Linux x86_64 or WSL2 with Git, make, curl, tar, xz, a native C compiler with address/undefined-behavior sanitizers, and Python 3.12 or newer. For example, on Ubuntu the system prerequisites are `git make curl xz-utils build-essential python3`. Docker and a system-wide ARM compiler are not required.

```sh
make setup             # explicitly fetch pinned sources and local build tools
make test
make check-tools       # locked Python environments, Ruff, strict mypy, tests
make build-default     # base QMK, fixed keymap, no VIA
make build             # both VIA release and debug packages; no flashing
make build-release     # reconstructed stock V15 behavior, no debug diagnostics
make build-debug       # stock behavior with split/sleep logging
```

`make setup` initializes QMK and only the four libraries required by this target: ChibiOS, ChibiOS-Contrib, LUFA and printf. It installs checksum-verified ARM GNU Toolchain 14.2.Rel1 and uv 0.8.22 under `.work/toolchains/`, and synchronizes the committed Python lockfiles. Compilation uses these prepared dependencies without downloading sources. Setup may need network access; subsequent builds do not. A complete `source.tar.gz` also works without Git metadata: it includes dependency sources and their integrity manifest, but still needs the compiler and Python packages provisioned.

Scripts load the root `.env`, or `.envrc` if `.env` is absent, through `scripts/project.sh`. They set QMK's source and configuration paths locally. Default parallelism is `JOBS=8`. `make build-vial` and `make build-zmk` fail clearly until implemented. No build, setup, test, or migration command accesses the keyboard.

`GMK70_SOURCE_PYTHON` optionally selects a Python-compatible launcher for source preparation and export (default: `python3`). A WSL launcher using native Windows Python must translate the returned build directory into a WSL path. Source manifests and archive member names use portable forward slashes on either host; dependency verification remains mandatory.

The two distributions are `artifacts/qmk/release/zuoya_gmk70_release.{bin,hex,elf,map}` and `artifacts/qmk/debug/zuoya_gmk70_debug.{bin,hex,elf,map}`. Each build exports the unchanged VIA JSON, checksums, compiler/linker flags, build log, dependency pins, source origins and a complete source archive. `SKIP_VERSION=yes` avoids embedded build timestamps; consult `build-info.txt` for provenance. The optional `make build-default` target remains a fixed-keymap development baseline. Old targets `make build-qmk-via` and `make build-diagnostic` alias release and debug respectively; old artifact folders are historical, not updated by these aliases.

The current behavior target is the supplied stock V15 image. Public GMK70 source fragments were found, but none was verified as that image's exact source. The reconstruction restores stock radio report delivery/retry handling, five-minute default idle sleep and held-key preparation, one-ms debounce, complete default keymaps and LED geometry, secondary decorative lighting, VIA commands, logical EEPROM layout and USB/Bluetooth metadata. See [the evidence, implemented behavior and remaining gaps](docs/STOCK-V15-RECONSTRUCTION.md). Complete stock equivalence and wireless hardware behavior remain unverified.

Both VIA distributions implement stock's deferred `0x0B` bootloader request. Debug additionally contains the split/sleep RAM logger; the optional split updater remains a separate debug USB feature. See [debug/release behavior](docs/DEBUG-RELEASE.md). These build commands do not operate on a keyboard.

Debug peer status uses unused bytes in the existing two-second mode reply, with no extra transactions. It no longer exposes battery/LED-buffer probes or measures SPI duration. [Diagnostic instructions](docs/BATTERY-DIAGNOSTICS.md) describe protocol 2 and the reader's compatibility with archived protocol 1 builds. The diagnostic reader remains getter-only and does not invoke the bootloader command.

`make clean` removes generated QMK trees while preserving maintained sources, dependency checkouts, installed tools and published artifacts. Each variant is built from a fresh composition, so edits and build flags cannot leak between variants. Dependencies with local modifications or incorrect revisions are rejected. `make format` formats project-owned sources and tooling.

QMK 0.34.4 changes EEPROM layout and its validity marker. Back up VIA mappings before any future firmware update; old on-device EEPROM data is not preserved across this upgrade. The firmware translates legacy RGB keycodes in imported maps to the new RGB Matrix keycodes. The vendor custom-key numbers and JSON remain unchanged. See [migration notes](docs/QMK-0.34-MIGRATION.md) for API and validation details.

## Original VIA JSON

Load [the original definition](definitions/via/ZUOYA_GMK70.json) through VIA's Design tab. Its contents are unchanged from the vendor package. The firmware uses explicit stock custom-key numbers, including `0x7E05` for 2.4 GHz, `0x7E06` for USB, and `0x7E1B` for battery (zero-based custom slot 27). Matrix brightness uses stock's `(value * 80) >> 8` conversion. Stock clamps effect 45, labelled Close All in the JSON, to enabled effect 44; the firmware follows that observed behavior.

Read [stock compatibility and migration](docs/STOCK-COMPATIBILITY.md) before updating an earlier project build. Loading JSON alone does not migrate saved mappings. The old compact keycode numbering conflicts with the restored stock numbering. An [offline converter](tools/keymap-migration/README.md) preserves normal keys, encoder assignments, macros and metadata while translating the old custom keys. Never run it on an original stock export. The V15 reconstruction also changes EEPROM offsets; see [the logical storage layout and installation limits](docs/STOCK-V15-RECONSTRUCTION.md#logical-eeprom-layout). Back up first, initialize the new firmware defaults after a future flash, and then import an appropriate VIA export.

The compiled Fn shortcuts select BT1/2/3 and 2.4 GHz with Fn+Q/W/E/R, pairing after a three-second hold in the appropriate physical mode. Fn+B displays battery in wireless mode. Fn+encoder press is transparent, as in stock; Fn+Backspace held for three seconds resets settings. Saved EEPROM maps can override these defaults. Select number-row digits from VIA's Basic category; numpad digits depend on Num Lock.

## Hardware and source provenance

| Component | Source / configuration |
| --- | --- |
| Starting port | uk-taniyama `8d8055bfc864b36d41bbfb0bf9cd393e4c4b6e20` |
| Upstream QMK | `0.34.4` / `08c662f286ddfd12a985f57b584b02eca5af0ae6` |
| Tri-mode reference | hangshengkeji `580665f77746c373ada7f79a4799f20a4f2f6c6d` |
| Keyboard target | `zuoya/gmk70` |
| MCU / bootloader | WB32FQ95 Cortex-M3 / WB32 DFU; ROM entry `0x1FFFE000` |
| Application memory | `0x08000000`, 128 KiB flash, 28 KiB RAM |
| USB identity | `342D:E491`, device version `0.3.0` |
| Split | UART1 A9/A10, 115200 full duplex; B9 handedness; direct USB on A7 takes master priority |
| Radio | UART3 C10/C11; source in `firmware/qmk-common/drivers/wireless/` |
| Matrix / LEDs | 10x9 matrix, 72 layout positions, 77 LEDs split 36/41 |
| Settings | SPI flash wear leveling, 8 KiB backing / 4 KiB logical |

This builds the keyboard MCU application. The radio module and USB receiver keep their own existing firmware. No vendor precompiled `.a` library is linked. Two inherited low-power machine-code arrays in `lpwr_wb32.c` remain unchanged and still require hardware validation.

Earlier repairs cover wireless host selection, raw-HID routing, rollover/key release, split isolation, USB sleep, stored board settings, mode-selector pins, and right-half USB master selection. See [mode-switch evidence](docs/STOCK-MODE-SWITCH.md), [right-half recovery](docs/RIGHT-HALF-RECOVERY.md), and [hardware acceptance tests](docs/HARDWARE-VALIDATION.md).

The earlier role bug blocked standalone right-half key processing. It is fixed in the source. The user confirmed successful right-half recovery through the vendor hardware pad procedure on 2026-09-24; runtime bootloader entry in the latest build still needs a separate hardware check.

## Validation limits

Native tests cover transport/report handling, all physical selector states, both halves' master selection, the stock custom-key contract, shortcut translation, and lighting packet routing. Migration tests exercise saved layers/encoders, metadata preservation, and input/output protection. Both QMK variants must compile; their HEX and BIN must contain the same application payload.

Compilation and tests do not prove full factory behavior on hardware. Wireless pairing, BT4/5 support in the installed radio, sleep/wake, RGB appearance, charging, and the latest recovery behavior require physical validation. Vial and ZMK support are future work, not delivered firmware.

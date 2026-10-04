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

Use Linux x86_64 or WSL2 with Git, make, curl, tar, xz, a native C compiler with address/undefined-behavior sanitizers, and Python 3.12 or newer. For example, on Ubuntu the system prerequisites are `git make curl xz-utils build-essential python3`. Docker and a system-wide ARM compiler are not required.

```sh
make setup             # explicitly fetch pinned sources and local build tools
make test
make check-tools       # locked Python environments, Ruff, strict mypy, tests
make build-default     # base QMK, fixed keymap, no VIA
make build             # both VIA release and debug packages; no flashing
make build-release     # ordinary VIA firmware, no remote bootloader command
make build-debug       # VIA with split/sleep logging and USB bootloader command
```

`make setup` initializes QMK and only the four libraries required by this target: ChibiOS, ChibiOS-Contrib, LUFA and printf. It installs checksum-verified ARM GNU Toolchain 14.2.Rel1 and uv 0.8.22 under `.work/toolchains/`, and synchronizes the committed Python lockfiles. Compilation uses these prepared dependencies without downloading sources. Setup may need network access; subsequent builds do not. A complete `source.tar.gz` also works without Git metadata: it includes dependency sources and their integrity manifest, but still needs the compiler and Python packages provisioned.

Scripts load the root `.env`, or `.envrc` if `.env` is absent, through `scripts/project.sh`. They set QMK's source and configuration paths locally. Default parallelism is `JOBS=8`. `make build-vial` and `make build-zmk` fail clearly until implemented. No build, setup, test, or migration command accesses the keyboard.

`GMK70_SOURCE_PYTHON` optionally selects a Python-compatible launcher for source preparation and export (default: `python3`). A WSL launcher using native Windows Python must translate the returned build directory into a WSL path. Source manifests and archive member names use portable forward slashes on either host; dependency verification remains mandatory.

The two distributions are `artifacts/qmk/release/zuoya_gmk70_release.{bin,hex,elf,map}` and `artifacts/qmk/debug/zuoya_gmk70_debug.{bin,hex,elf,map}`. Each build exports the unchanged VIA JSON, checksums, compiler/linker flags, build log, dependency pins, source origins and a complete source archive. `SKIP_VERSION=yes` avoids embedded build timestamps; consult `build-info.txt` for provenance. The optional `make build-default` target remains a fixed-keymap development baseline. Old targets `make build-qmk-via` and `make build-diagnostic` alias release and debug respectively; old artifact folders are historical, not updated by these aliases.

Custom(27)/Custom(7) now use the stock V13/V15 event protocol: A4/A5 on battery press/release and F0/F1 on charging press/release. Both halves draw the battery bar from the master's percentage, carried by the existing two-second mode/status exchange. There is one send attempt per key event, with no additional retry, expiry, periodic refresh, or held-state extension. The LED output remains the asynchronous baseline that the user confirmed works locally. Global `sym_defer_g` debounce at 5 ms, stock keycodes, and flashing/wireless repairs remain. See [the stock behavior and historical repair notes](docs/BATTERY-FIXES.md).

The October 1 global sleep experiment was removed. On October 3, a device capture established repeated false wakes with cause NONE; the current build restores only stock's filter that keeps STOP for that cause. Other `release`/`default` behavior outside the battery keys and debounce follows the integrated pre-battery baseline `c5c24d42`, including its existing VIA and board/radio compatibility work. Both halves have since been updated and read back successfully. The user confirmed idle lighting stayed off and a right-half key woke both halves; broader wireless stability remains under validation. See [scope and validation limits](docs/BATTERY-FIXES.md).

The `debug` variant retains the split/sleep RAM logger and adds direct-USB VIA command `0x0B` to enter the bootloader. The explicit command acknowledges, waits 100 ms, and calls QMK's reset-to-bootloader function without invalidating EEPROM. It adds no periodic task and cannot be invoked through the radio. Release and the optional fixed-keymap baseline contain neither feature. See [debug/release behavior and command format](docs/DEBUG-RELEASE.md).

Debug peer status uses unused bytes in the existing two-second mode reply, with no extra transactions. It no longer exposes battery/LED-buffer probes or measures SPI duration. [Diagnostic instructions](docs/BATTERY-DIAGNOSTICS.md) describe protocol 2 and the reader's compatibility with archived protocol 1 builds. The diagnostic reader remains getter-only and does not invoke the bootloader command.

`make clean` removes generated QMK trees while preserving maintained sources, dependency checkouts, installed tools and published artifacts. Each variant is built from a fresh composition, so edits and build flags cannot leak between variants. Dependencies with local modifications or incorrect revisions are rejected. `make format` formats project-owned sources and tooling.

QMK 0.34.4 changes EEPROM layout and its validity marker. Back up VIA mappings before any future firmware update; old on-device EEPROM data is not preserved across this upgrade. The firmware translates legacy RGB keycodes in imported maps to the new RGB Matrix keycodes. The vendor custom-key numbers and JSON remain unchanged. See [migration notes](docs/QMK-0.34-MIGRATION.md) for API and validation details.

## Original VIA JSON

Load [the original definition](definitions/via/ZUOYA_GMK70.json) through VIA's Design tab. Its contents are unchanged from the vendor package. The firmware uses explicit stock custom-key numbers, including `0x7E05` for 2.4 GHz, `0x7E06` for USB, and `0x7E1B` for battery (zero-based custom slot 27). Brightness uses the original 0–255 slider, scaled by QMK to the LED maximum of 80. Effect 45, labelled Close All, disables lighting.

Read [stock compatibility and migration](docs/STOCK-COMPATIBILITY.md) before updating an earlier project build. Loading JSON alone does not migrate saved mappings. The old compact keycode numbering conflicts with the restored stock numbering. An [offline converter](tools/keymap-migration/README.md) preserves normal keys, encoder assignments, macros and metadata while translating the old custom keys. Never run it on an original stock export. Back up first, initialize the new firmware defaults after a future flash, and then import a converted VIA export.

The compiled Fn shortcuts select BT1/2/3 and 2.4 GHz with Fn+Q/W/E/R, pairing after a three-second hold in the appropriate physical mode. Fn+B displays battery in wireless mode. Fn+encoder press requests the bootloader; Fn+Backspace held for three seconds resets settings. Saved EEPROM maps can override these defaults. Select number-row digits from VIA's Basic category; numpad digits depend on Num Lock.

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

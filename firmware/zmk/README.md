# GMK70 ZMK left USB trial

`make build-zmk` builds the actual pinned `dependencies/zmk/app` for
`gmk70_left`, using this repository's WB32FQ95 SoC, GPIO and USB integration.
This is an experimental left-side staging trial. Hardware execution and USB
operation require the separate qualification workflow; compiling the port does
not establish those results.

The [2026-10-27 live qualification](../../docs/ZMK-LEFT-FIRST-TRIAL.md) flashed this
left trial, observed its unique keyboard/consumer USB identity, and verified
automatic watchdog return to Vial. Physical key input and both-half ZMK operation
remain untested. The right application and both residents were preserved.

Initialize sources and build tools explicitly:

```sh
make setup-zmk
make build-zmk
```

`make setup` provides the shared pinned ARM toolchain if it is not installed.
Both ZMK scripts load `scripts/project.sh`. Setup fetches only exact locked
commits. The build verifies clean checkouts and performs no dependency downloads
or connected-device operations. ZMK, Zephyr, CMSIS and LVGL stay unmodified under
`dependencies/`. See [PROVENANCE.md](PROVENANCE.md) for their upstream manifests,
licenses and vendor register references.

The initial target uses a 96 MHz Cortex-M3, awake polling of the physical left
5-by-8 ROW2COL matrix, and native USB HID. It has no radio adapter, wired split,
settings storage, RGB, encoder rotation, low-power support or Studio integration.
Stock radio firmware stays unchanged. Ordinary left keys follow the maintained
board's physical matrix; stock macro positions are unbound.

The image links directly at staging address `0x08020000` with a 120 KiB maximum.
RAM begins at `0x20000040`; Zephyr's KiB-granularity linker configuration uses
27 KiB, ending at `0x20006C40`, below the retained record at `0x20006FC0`.
The image checker enforces all ELF load segments, initialized data sources,
initial stack and reset vector against the flash/RAM bounds, and requires real
ZMK keymap/USB and WB32 symbols. The trial leaves the launcher's watchdog active
and never feeds it.

Generated build files are under `.work/zmk/left-trial/normal/`. Artifacts are
under `artifacts/zmk/left-trial/`, including BIN/HEX/ELF/map, configuration,
devicetree, exact dependency lock, compiler inputs, image-check results and
hashes. These are staging trial artifacts, not ordinary application/updater
release images. Build and test commands do not flash, reset or write to a keyboard.

The [feasibility investigation](../../docs/ZMK-FEASIBILITY.md) records future
split/radio work and hardware constraints. The
[trial launcher](../../docs/ZMK-TRIAL-LAUNCHER.md) describes the separate resident
preservation and recovery workflow.

`make build-zmk-trial-fixtures` still builds the separate bare Cortex-M3 return
and hang fixtures under `artifacts/zmk/trial-fixtures/`. Those fixtures are not
ZMK firmware.

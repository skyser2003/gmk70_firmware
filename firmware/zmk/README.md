# GMK70 ZMK wired development and staging trials

The [exact RAM extent](../../docs/ZMK-RAM-EXTENT.md) exposes 28,544 bytes while
preserving both retained records. The recorded universal image keeps identical
application bytes and gains 896 bytes of available space for future allocations.

Current checkpoint: [battery indicators and saved layout import](../../docs/ZMK-BATTERY-LAYOUT.md)
installs LEFT 74,020/3805544436 with RIGHT unchanged at 43,752/2368050678.
Battery telemetry and both visible held actions work. The user's 275 supported
Vial bindings and 13 explicitly unassigned unsupported bindings are saved in
Studio; all 288 were reloaded and independently compared. Factory defaults are
unchanged. Both halves passed final 70-second zero-feed health, with unchanged
residents and no reconnect. Normal Studio connects with four layers.
The earlier checkpoints below record preceding installations.

Current checkpoint: [right RGB restoration](../../docs/ZMK-RIGHT-RGB.md) installs native split RGB
on both halves (LEFT 70,672/1286125312; RIGHT 43,752/2368050678). Both returned
automatically and passed 70-second zero-feed health, with unchanged residents and
the user's freshly saved Studio map preserved. The user confirmed right LEDs work
and Fn+X controls both halves together.
The earlier checkpoints below record the preceding installations.

Current checkpoint: [permanent watchdog removal](../../docs/ZMK-PERMANENT-WATCHDOG-REMOVAL.md)
applies to canonical builds and both installed halves: LEFT 70,196/986599886,
RIGHT 38,160/1887741245, resident CRCs 1770733249. Both passed 70-second zero-feed
health; Studio saving and the original four-layer map were verified. RIGHT
needed one joined-LEFT cold cycle for the old-watchdog transition. Earlier
experiment/build/state notes below are historical; use the linked build targets.

Earlier 2026-10-29 checkpoint: [LEFT watchdog rollback](../../docs/ZMK-WATCHDOG-ROLLBACK.md) passed two
automatic original-SYSRESETREQ updates and Studio save/reload. Installed LEFT is
70,340/1796245169 with no watchdog; RIGHT and residents are unchanged. This
supersedes the installed-state notes below. The exact reset/retention mechanism
remains unmeasured; ordinary watchdog-enabled variants are unchanged.

Current hardware checkpoint: the [LEFT resident-handoff variant](../../docs/ZMK-RESIDENT-HANDOFF.md) runs four-layer Studio/local RGB and passed automatic A→B→A activation. RIGHT remains unchanged. Ordinary builds retain SYSRESETREQ; use the explicitly labeled handoff targets for the qualified LEFT experiment.

Current sources add an offline [four-layer/RGB candidate](../../docs/ZMK-MAC-LIGHTING.md).
It has not been installed or hardware-qualified; the verified checkpoint below
remains unchanged. Read the memory/stack and restart limits before installation.

The [Studio central](../../docs/ZMK-STUDIO.md) is now installed and verified on
LEFT, with the original permanent RIGHT unchanged. The local Studio app connects,
saves bindings/layer names, and preserves them across a distinct-image LEFT
update. The original Base/Fn map was restored. Build it with
`make build-zmk-wired-studio-updater`; it is a separate unlocked development
variant with standard CDC, Feature updater and a 16 KiB settings reservation.
See [Studio validation](../../docs/validation/2026-10-28-zmk-studio/README.md).

The earlier permanent milestone/status and updater verification/fallback workflow are
recorded in [Permanent wired ZMK](../../docs/ZMK-PERMANENT-WIRED.md) and its
[sanitized evidence](../../docs/validation/2026-10-28-zmk-permanent/README.md).
At that earlier checkpoint, both halves ran normal permanent A, with final A/A readback and
61 seconds of both-half health. Distinct-image left/right updates through LEFT
USB passed. Automatic restart remains unresolved; joined-left cycling is a
tested fallback after a restart timeout, and a transfer ACK alone is not success.

Native USB/B1 tests require a host C++20 compiler (`c++`) as well as a C compiler.

## Experimental permanent wired updater

The universal `gmk70_wired` board runs the pinned ZMK application with a combined
10-by-9 matrix, the stock physical key order and Fn at left row4/column2. B9
selects the half; C14/C15 select wired mode. Keyboard HID0 and updater HID1 use
`342D:E491`, manufacturer `HS`, and the updater's `FF60:61` usage. The native
live worker owns deferred flash/settings work; permanent firmware never starts or feeds a watchdog. This is an
explicitly unlocked development build; it is not a normal release asset.

```sh
make build-zmk-wired-updater
make build-zmk-wired-update-test
make build-zmk-wired-qualification
make test-zmk
```

Permanent A/B images link at `0x08002000`, retaining the resident and all saved
EEPROM. Their only configuration difference is the product string:
`GMK70 ZMK Wired` versus `GMK70 ZMK Wired Update Test`. Each uses its own
`.work/zmk/<variant>/updater/` cache and `artifacts/zmk/<variant>/` output.
Packaging requires the existing reviewed
`artifacts/qmk/debug-updater/resident.bin`: its **padded** 8192-byte SHA256 must be
`be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`
and CRC32 `1770733249`. ZMK never rebuilds this prerequisite. Application-only
packages have `profile=zmk` and load through `tools/firmware-update/update.py`;
there is no generated direct-USB installer.

Qualification links at `0x08020000`, identifies as `GMK70 ZMK Wired Qualification`,
never feeds the inherited watchdog, and permits no running-code write or durable
install. It emits only `left/` and `right/` target-specific `TrialPackage` outputs;
it must enter through the separate trial workflow. No permanent target wraps main.

`tools/firmware-update/read_health.py --target left --duration 60 --output <path>`
collects read-only runtime evidence; use `--target both` after both halves run
ZMK. Run it from the firmware-update uv project/environment after loading
`scripts/project.sh`. It requires advancing uptime/feed calls for at least 60
seconds after uptime exceeds 2 seconds, with completed samples at most 2 seconds
apart per half. Transport and relay retries are counted separately; no reconnect
or reset is attempted. Zero matrix events means unavailable, not physical input
evidence. `--inspect-only --qualification` reads bounded staging identity/health
without claiming permanent watchdog service. The callable `HealthReader` accepts
an existing link so qualification scripts can share one HID handle.

Radio, encoder rotation and power saving remain outside this wired
milestone. RGB is present in the current uninstalled candidate; Studio and persistent settings are provided by the separate Studio
central described above. Building/native testing cannot establish
silicon timing, USB/UART behavior or physical key input. An inherited permanent
IWDG also makes arbitrary warm rollback to frozen Vial unqualified; retain the
separate recovery procedure and do not fabricate a trial record.

## Existing staging trials

`make build-zmk` builds the actual pinned `dependencies/zmk/app` for
`gmk70_left`, using this repository's WB32FQ95 SoC, GPIO and USB integration.
This is an experimental left-side staging trial. Hardware execution and USB
operation require the separate qualification workflow; compiling the port does
not establish those results.

The [2026-10-27 left qualification](../../docs/ZMK-LEFT-FIRST-TRIAL.md) flashed this
left trial, observed its unique keyboard/consumer USB identity, and verified
automatic watchdog return to Vial. That historical trial did not test physical key input or simultaneous both-half
ZMK operation; see the current status above. That left sequence preserved the right application and both residents.

Initialize sources and build tools explicitly:

```sh
make setup-zmk
make build-zmk
make build-zmk-right-trial
```

`make setup` provides the shared pinned ARM toolchain if it is not installed.
Both ZMK scripts load `scripts/project.sh`. Setup fetches only exact locked
commits. The build verifies clean checkouts and performs no dependency downloads
or connected-device operations. ZMK, Zephyr, CMSIS and LVGL stay unmodified under
`dependencies/`. See [PROVENANCE.md](PROVENANCE.md) for their upstream manifests,
licenses and vendor register references.

Both targets use a 96 MHz Cortex-M3. The left target uses awake polling of its physical
5-by-8 ROW2COL matrix, and native USB HID. It has no radio adapter, wired split,
settings storage, RGB, encoder rotation, low-power support or Studio integration.
Stock radio firmware stays unchanged. Ordinary left keys follow the maintained
board's physical matrix; stock macro positions are unbound.

The image links directly at staging address `0x08020000` with a 120 KiB maximum.
RAM begins at `0x20000040`; Zephyr's KiB-granularity linker configuration uses
27 KiB, ending at `0x20006C40`, below the excluded upper range at `0x20006FC0`.
The launcher record occupies the excluded first 64 bytes at `0x20000000`.
The image checker enforces all ELF load segments, initialized data sources,
initial stack and reset vector against the flash/RAM bounds, and requires real
ZMK keymap/USB and WB32 symbols. The trial leaves the launcher's watchdog active
and never feeds it.

Generated build files are under `.work/zmk/left-trial/normal/`. Artifacts are
under `artifacts/zmk/left-trial/`, including BIN/HEX/ELF/map, configuration,
devicetree, exact dependency lock, compiler inputs, image-check results and
hashes. These are staging trial artifacts, not ordinary application/updater
release images. Build and test commands do not flash, reset or write to a keyboard.

## Right-side main-return trial

`make build-zmk-right-trial` builds the actual ZMK application for `gmk70_right`,
using the maintained right 5-by-9 matrix and the same SoC/GPIO/USB drivers. Output
is under `artifacts/zmk/right-trial/` and `.work/zmk/right-trial/normal/`.

This target enables `CONFIG_GMK70_TRIAL_MAIN_RETURN`. Zephyr calls an opt-in
linker wrapper which calls real ZMK main. Only after main returns zero and the
chosen matrix device is ready does it sleep for 500 ms and request a software
reset. An earlier hang or initialization failure leaves IWDG to expire. The
image checker verifies startup calls the wrapper and the wrapper calls real main.
The left target excludes this wrapper and returns through the watchdog instead.

This provides execution proof through the right trial RESULT over left USB.
It does not establish right USB typing, physical matrix events or native split
operation. The right trial never feeds IWDG or writes settings/flash.

The [2026-10-27 right qualification](../../docs/ZMK-RIGHT-FIRST-TRIAL.md) flashed
and ran this image through left USB, observed the expected post-main software
reset, and verified 60 seconds of continuous recovery uptime/feed activity.
Watchdog, bare return and bare hang checks also passed. Both halves returned to
Vial. The right updater upgrade needed one joined-left USB reconnect; no pad
bridge or direct right USB flashing was needed. The left application and both
residents stayed unchanged throughout the right qualification.

The [feasibility investigation](../../docs/ZMK-FEASIBILITY.md) records future
split/radio work and hardware constraints. The
[trial launcher](../../docs/ZMK-TRIAL-LAUNCHER.md) describes the separate resident
preservation and recovery workflow.

`make build-zmk-trial-fixtures` still builds the separate bare Cortex-M3 return
and hang fixtures under `artifacts/zmk/trial-fixtures/`. Those fixtures are not
ZMK firmware.

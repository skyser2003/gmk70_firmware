# Experimental Vial port, 2026-10-09

The first GMK70 Vial application builds from the shared board/radio sources.
This is an initial port with software validation; it has not been installed or
validated on a keyboard. `make build-vial` produces normal images under
`artifacts/vial/vial/`; `make build-vial-updater` produces an application for
the existing resident split updater under `artifacts/vial/vial-updater/`.

## Source and protocol boundaries

The unmodified Vial fork is independently pinned to
[`dd43959ae5c08d8a28d38a1acf7b04e86b14a344`](https://github.com/vial-kb/vial-qmk/commit/dd43959ae5c08d8a28d38a1acf7b04e86b14a344),
the advertised `vial` snapshot dated 2026-07-26. No firmware tags were advertised
on 2026-10-09. `dependencies/sources.lock.json` records this commit and its own
ChibiOS, ChibiOS-Contrib, printf and LUFA pins. Builds use those exact revisions
without fetching or following the branch. Official QMK's 0.34.4 pin is unchanged.

The maintained keymap, UID, unlock keys and definition live in
`firmware/qmk-vial/keymaps/vial/`; the compatibility adapter lives in
`firmware/qmk-vial/compat/`. Builds read these and the common hardware sources
directly, with generated files under `.work/vial/vial/<normal|updater>/`.
The build adapter records file origins and rejects source collisions. Sources
are neither copied nor symlinked, and neither upstream framework is edited.

The application uses the fork's VIA protocol 9 and Vial protocol 6 handlers,
including native unlock checks, keymap/macros/encoder commands and VialRGB.
It does not advertise stock VIA protocol 12 or use the stock VIA adapter.
The small wrapper records board activity and retains stock sleep-selector and
NKRO controls. Updater builds additionally handle the custom `0x7F` updater
command; all other messages reach the native Vial handler.

Project UID: `b691d7277fc88251`, generated once for this port. Physical unlock
requires holding **Escape + left Shift**, matrix positions `(0,1)` and `(3,1)`,
as requested by the Vial GUI. Insecure unlock is not enabled. The embedded
layout is derived from the unchanged stock VIA definition. Stock custom slots
cover every value `0x7E00` through `0x7E21`, retaining reserved gaps. Vial-only
manual-control aliases follow at `0x7E22` through `0x7E2E`. Known
functions have readable labels. Reserved slots 8, 10–16 and 20–25 use empty
button labels, with `Unused (CUSTOM(N))` tooltips identifying their original
numbers. Vial still displays these as blank selectable buttons; they are not
removed from the list. Existing aliases and numeric keycodes are unchanged.
Tooltips contain only the basic role name and `CUSTOM(N)`. Pairing instructions,
manual shortcuts and detailed behavior remain in this guide and the manual-key
reference instead of the picker descriptions.

### Custom-key label evidence

The manufacturer's bilingual [GMK70 instruction leaflet, mirrored on
Manuals+](https://manuals.plus/m/afce334373cd4a880f56d8db405f46f6028be78bc31c28f94df390d428666f4c.pdf)
documents the following functions. The manufacturer's website was unavailable
when checked. The leaflet gives Fn shortcuts rather than numeric custom IDs;
their IDs are matched against the stock keymap and dispatch evidence in
[STOCK-COMPATIBILITY.md](STOCK-COMPATIBILITY.md).

| Stock slot | Vial label | Leaflet shortcut/function |
| --- | --- | --- |
| `CUSTOM(7)` / `0x7E07` | Show Charging | Hold Fn+left Space; indicator shows charging/full |
| `CUSTOM(9)` / `0x7E09` | Logo Effects | Fn+Backslash; cycle logo lighting |
| `CUSTOM(26)` / `0x7E1A` | Num/F Toggle | Fn+left Ctrl; switch number-row/F-key behavior |
| `CUSTOM(27)` / `0x7E1B` | Show Battery | Hold Fn+B; number-row LEDs display battery level |

The six shortcut slots 28–33 are named Undo, Cut, Copy, Paste, GUI+Tab and GUI+E
from their verified stock handlers, not from a custom-ID table in the leaflet.
The GUI combinations retain literal names because their actions vary by OS.
Slots 17–19 are the stock firmware's undocumented special USB sleep request
controls; their labels do not imply an idle-timeout setting. Bluetooth 4/5
labels are retained from the vendor JSON, while the leaflet describes 1–3.
Undocumented reserved slots are left reserved rather than assigned a function.

All 22 keyboard-specific controls in the leaflet are available together in the
User tab, including named Windows/Mac mode, Windows-key lock and RGB controls.
The [complete manual-key reference](VIAL-MANUAL-KEYS.md) lists their stable values,
standard F/media/navigation keys already available in Vial, and the hardware-only
operations. The new aliases reuse board handlers and do not rewrite saved maps.

To try these labels without updating firmware, use the maintained `vial.json`
with desktop Vial's **File → Sideload VIA JSON…** and select the `[sideload]`
device. This provides a local VIA-mode definition, which hides Vial-only controls
such as dynamic combos and QMK settings. Normal Vial mode uses the definition
embedded in the firmware; the next build includes these labels.

### Shared behavior and Vial features

The build shares all four stock default layers, encoder assignments,
LED geometry, wireless/power fixes and board controls. It enables:

- Live QMK settings: Auto Shift, tap/hold timing, Permissive Hold,
  Hold On Other Key Press, Retro Tapping, Chordal Hold, Quick Tap, Flow Tap,
  one-shot settings and mouse-key tuning.
- Dynamic tap dances, combos and key overrides: 32 entries each.
- Caps Word, Layer Lock, Repeat and Alternate Repeat, including 32 dynamic
  alternate-repeat rules.
- Extended 16-bit keycode macros and VialRGB, including direct LED control.

Dynamic tables start empty. Assign their keys/rules in the Vial GUI. Auto Shift
and Flow Tap are compiled but initially off, following native Vial defaults.
Caps Word, Layer Lock and Repeat keys are available for assignment without
replacing the stock default keymap. VialRGB uses the native fork's lighting API;
matching every stock lighting control or animation across forks is unverified.

During normal operation, each half reconciles its LED power switches with the
native synchronized RGB enable and suspend state. Held battery-level and
charging-status queries also power the LEDs and render their indicators when
RGB is off. Releasing the query clears the temporary frame and restores darkness
without changing the saved RGB enable state, effect or brightness. Sleep and
USB suspend retain ownership of LED power; queries do not override them.
This lets the right half recover automatically if its local startup setting had
RGB disabled, and lets
VialRGB Off/On control the physical LED supply. Stock sleep/wake callbacks retain
control of the supply during sleep preparation, STOP and wake recovery.

To remap the knob, open **Keymap** and select a layer. Two circular controls
above the top-left knob button represent counterclockwise (left) and clockwise
(right) rotation. Select a direction and assign a keycode in the lower panel.
The defaults are Volume Down and Volume Up on all four layers. The rectangular
knob button below these controls edits its press action independently. The
embedded layout uses Vial's separate encoder-direction entries; the stock VIA
definition and physical matrix layout remain unchanged.

The Vial adapter tracks activity and physical held keys in
`pre_process_record_kb`, before combos and tapping can consume/replay records.
Synthetic combo/macro/repeat output does not decrement physical held keys.
This preserves the board's idle-sleep gate while using advanced features and
requests wired host wake even when a key is consumed by a feature. QMK/VIA
keeps its original hook. The stock four-key saturation policy is unchanged.

## EEPROM boundary

Vial uses its native EEPROM layout, rather than the stock V15 packed layout.
The stock core/RGB/magic wrappers and fixed map/macro offsets are excluded from
Vial builds. The shared board's radio/flags continue using native KB/user words.
Four layers, one encoder and 16 macros fit within the existing 4 KB logical
EEPROM. QMK settings occupy 40 bytes, tap dances/combos/key overrides each use
320 bytes, and alternate-repeat rules use 192 bytes. The 16 macros share the
remaining **2123 bytes**. Dynamic storage ends at `0xFFB`. A four-byte Vial
schema marker `0x564D4702` is reserved at `0xFFC`–`0xFFF`, outside the macro region.

The core validity wrapper rejects storage without this marker. A first future
installation over stock/VIA data therefore initializes Vial defaults instead
of interpreting old mappings, RGB words or macro bytes using the wrong offsets.
A previous Vial baseline with marker `0x564D4701` is also rejected because
enabling these extensions relocates macro storage. A settings backup and an
explicit migration are needed before installing it.
No backup import/converter between these layouts is implemented yet. No build
or test writes, resets or flashes a physical keyboard.

## Build and validation

```sh
make setup        # existing compiler and Python tools, official QMK sources
make setup-vial   # explicitly initialize the independently pinned Vial sources
make build-vial   # normal Vial image; no keyboard access
make build-vial-updater # resident-compatible Vial application; no keyboard access
make test
make check-tools
```

The distribution contains a firmware ZIP with BIN/HEX, the embedded definition,
installation/storage notes and checksums. It also exports ELF/map, source
origins, flags, checksums and a complete source archive with the selected Vial
framework and libraries. An exported Vial source archive verifies its own
source manifest without Git metadata; `make setup` recognizes its selected
framework before installing build tools. `make clean` removes generated QMK and
Vial trees while preserving maintained sources, dependencies and artifacts.
Vial debug/diagnostic builds are not implemented.

Software validation passed the native suite, Ruff, strict mypy and repository/
tooling tests. New regressions cover dependency isolation, shared source paths,
physical unlock configuration, custom-code gaps, schema rejection, bounded
packets, and delegation of Vial identification/unlock/bootloader messages.
The activity regression covers both halves, consumed user/feature records,
synthetic releases, encoder events, wired wake and the physical held-key gate.
Bounded execution of the compiled Vial ARM code also verified protocol versions,
UID, all compressed-definition pages, reported unlock coordinates, stock tables,
every native dynamic keycode, EEPROM schema initialization, and macro allocation
without overwriting the marker. Native timed unlock and locked/unlocked
macro writes and bootloader requests also passed, with matrix/time/reset I/O
intercepted. The full-feature image is 86,980 bytes within the 128 KB normal
flash region. All 128 dynamic slots round-tripped through the native protocol;
out-of-range entries and locked bootloader mappings were rejected. Live QMK
settings enumerated and persisted through reinitialization; native defaults,
empty-table reset, direct LED bounds, consumed combo events with the physical
held-key sleep gate, and all three extended macro opcodes passed. All four QMK distributions rebuilt to byte-identical firmware
compared with the signed stock-fix build. Evidence is under
`artifacts/analysis/vial-features-2026-10-09/` (the earlier baseline is preserved
separately). This is an instruction harness
with simulated EEPROM/clock/USB, not an official or complete device emulator.

## Next port work

- Add read-only settings/log relay and persistent diagnostics
  with Vial-aware EEPROM decoding and explicit migration from the stock layout.
- Validate physical USB/Vial GUI identification, unlock, mapping/macros, encoder,
  VialRGB, Bluetooth/2.4 GHz reports, split roles and idle/wake recovery on both
  halves. Both halves must use the same framework/build; mixed split firmware
  is not verified.

The stock fixes pushed in `9d8b07e` and their debug-updater package are separate
from this Vial port. Full stock equivalence and hardware recovery remain
unverified on either framework.

## Vial split updater

The updater uses the shared ABI 1 image receiver and resident installer. Connect
only the left USB half in wired mode and keep the right powered and linked.
The host checks both halves, installs and verifies right, then installs left.
The Vial resident binary is byte-identical to the existing QMK debug updater's
resident binary, with padded boot CRC `0x698B3EC1`. Compiled split transaction
IDs, shared-memory size/offsets and RPC buffers also match the QMK updater:
19 transactions, `USER_SYNC_MMS=17`, `GMK_UPDATE_RPC=18`, 32-byte RPC buffers.
This supports the protocol transition during an update; physical QMK-to-Vial
relay installation is still unverified. Both halves must end on Vial updater.

`gmk70-vial-updater.zip` contains `application.bin/.hex` at `0x08002000`,
`update.json`, the matching `resident.bin`, initial `install.bin/.hex`, Vial's
definition, a standalone host script, dependency requirements and checksums.
Use `application.bin` for an existing compatible resident updater. Initial
`install.*` installation requires separate direct access to each 256 KB half;
see [the updater guide](SPLIT-UPDATER.md). The normal Vial image is not an updater
application. Vial normal/updater builds share schema `0x564D4702`; migrating from
stock/VIA still resets settings and requires a backup before installation.
The unmodified Vial fork also generates a random `BUILD_ID` for each build and
uses it as its VIA storage magic. An independently rebuilt image therefore
resets dynamic mappings and macros, even with the same schema. Export your
configuration in Vial before updating; preserving maps across builds is not
part of this updater-only change.

On Vial, INFO/STATUS are available while locked. Firmware writes require native
Vial unlock: open the Vial GUI, hold **Escape + left Shift** when prompted, then
close Vial without unplugging or resetting. Authorization is checked on the left
USB master before local staging or peer relay; the right inherits it over the
split link. Wireless-delivered updater requests and USB requests with the mode
switch in wireless mode are rejected. No insecure-unlock build option is used.

```sh
bash scripts/update-device.sh artifacts/vial/vial-updater --target both --inspect
# Explicit device operation, after unlocking Vial and closing the GUI:
bash scripts/update-device.sh artifacts/vial/vial-updater --target both --install
```

Only the updater is added. EEPROM fetching/backups, RAM diagnostics and persistent
logging are excluded. Settings/log query operations return unavailable, and no
query handler is linked. No updater traffic or flash writes occur while idle.
Native sanitizer tests cover locked access, local/peer transfers through
seal/commit and delayed reset, wired/radio/role gates, unavailable queries and
no idle traffic. Host regressions cover Vial packages, unlock errors, resident
checks, right-first ordering and stopping before left if right restart
verification fails. Compilation and simulated tests do not validate flash
programming on hardware. The application is under 91 KB within the 120 KB partition.
Bounded execution of the compiled ARM image also passed the real Vial timed
physical-unlock handshake, locked local/peer writes, wired-mode rejection,
unavailable queries, local staging/seal/commit and delayed reset. EEPROM,
matrix/clock and flash hardware were simulated; large INFO CRCs were calculated
by the host. Evidence is under `artifacts/analysis/vial-updater-2026-10-09/`.

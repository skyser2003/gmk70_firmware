# Original VIA definition compatibility

Current V15 reconstruction status, EEPROM layout changes and verification limits are documented in [STOCK-V15-RECONSTRUCTION.md](STOCK-V15-RECONSTRUCTION.md). The historical behavior notes below describe earlier revisions where they conflict with that reconstruction.

The target is the unchanged vendor `definitions/via/ZUOYA_GMK70.json`, SHA-256 `e878d51b9c1bdbf182feb48fa892ad822c9b4b8ff94f42ebe6b5e7fd0ba94789`. USB VID/PID `342D:E491`, the 10x9 matrix, physical layout, and encoder coordinate already matched. The prior compact custom-key enum did not.

## Stable custom keycodes

| Code | Stock meaning | Evidence |
| --- | --- | --- |
| `0x7E00`–`0x7E04` | Bluetooth devices 1–5 | Original JSON entries 0–4 |
| `0x7E05` | 2.4 GHz | Original JSON entry 5 |
| `0x7E06` | USB | Original JSON entry 6 |
| `0x7E07` | Charging-status display | V13 dispatch to `0x08005BCE` |
| `0x7E09` | Lighting mode | V13 dispatch to `0x08005B0E`, calls `0x08004FE8` |
| `0x7E1A` | Function/media-key flip | V13 dispatch to `0x08005AEA`, toggles persistent board configuration |
| `0x7E1B` | Battery level | V13 dispatch to `0x08005B96`, held indicator with device-mode guard |
| `0x7E1C`–`0x7E1F` | Ctrl+Z/X/C/V | V13 handlers at `0x080057C2`, `0x080057E2`, `0x08005802`, `0x08005822` |
| `0x7E20`, `0x7E21` | GUI+Tab, GUI+E | V13 handlers at `0x08005776`, `0x0800578E` |

The stock binary SHA-256 is `857a669c1fc739cd18ccd9a5526e80db10b150fbdea344b36031105e72e661f3`. The custom-key branch at `0x08005356` subtracts `0x7E07`; its 27 halfword entries start at `0x08005366`, with target `0x08005366 + 2 * entry`. Data in that table must not be interpreted as Thumb instructions.

Battery is slot **27 counted from zero**, `0x7E1B`. An earlier troubleshooting explanation incorrectly translated “Custom 27” as `0x7E1A`; the binary distinguishes these handlers. Raw hex values are the unambiguous reference.

The stock default matrix keymap starts at binary offset `0x126C0`; its Windows Fn+B entry at `0x127B6` is also `0x7E1B`. The function-flip flag set by `0x7E1A` is read by the number-row handler at `0x0800585E`, which chooses F1 or the alternate system shortcut.

The explicit enum in `firmware/qmk-common/keyboards/zuoya/gmk70/stock_keycodes.h` preserves gaps instead of renumbering later keys. Unused slots remain unhandled. Bluetooth 4/5 and USB now have handlers. As a deliberate change from stock shared with ZMK, connection shortcuts may select any destination regardless of the physical switch position. The switch selects its corresponding connection at startup or after a debounced move, without continually overriding shortcuts; invalid pin states still reject new selections. Firmware writes retain the physical wired-mode gate. The radio module's actual support for five simultaneous stored Bluetooth pairings remains a hardware-validation item. Wireless selection/pairing retains the three-second hold; moving the switch, releasing the held position or editing the keymap cancels pending pairing, while unrelated key events do not replace the pending pairing key.

## Repository safeguards beyond stock reconstruction

NKRO defaults on through native configuration, with a persistent Fn+V toggle on
both Windows and Mac defaults. The Mac binding is an intentional change from
stock's transparent cell. Existing saved maps are preserved. See
[NKRO](NKRO.md) for format transitions, ZMK parity and radio limits.

VIA and Vial share these board safeguards, adapted from this repository's ZMK
implementation. Custom-key numbers, persistent flags and native settings schemas
are unchanged.

- Function/media flip remembers the output selected on key press and releases
  that output after a flip or OS change. Plain number/function keys keep native
  processing when no remapping is needed. Shortcut modifiers are merged into
  outgoing report copies, independently of native real and weak modifiers.
- Battery queries track physical positions. Multiple bindings remain active
  until their last release, including release after live keymap replacement.
  The USB guard blocks a new percentage query, but releasing an accepted query
  still clears it after changing modes. Existing A4/A5 and F0/F1 packets are sent
  once per aggregate transition, with no retry or reconnect protocol.
- Only radio percentages from 0 through 100 are accepted. Samples older than ten
  seconds are unknown; the existing two-second `0x55` exchange uses 255 for an
  unknown reading. Unknown percentage bars are purple and do not cause a
  low-battery warning or shutdown. Both halves should run matching firmware.
- Radio framing rejects invalid raw subtypes, quarantines oversized or damaged
  raw tails with a finite byte budget, and expires partial ordinary frames after
  ten milliseconds. Host/channel changes send zero keyboard, consumer, system
  and mouse reports before selecting the new destination. Same-channel wake
  does not release held input.
- Deferred updater work rechecks wired mode, USB activity, role and native Vial
  authorization. Right peers inherit authorization from the USB master. Reads
  and rejected mutations do not renew an unfinished upload's ten-second lease.

The held-position bitsets in `qmk-common/.../battery_holds.c` are also used by
ZMK. Framework event hooks, keycode adapters, radio I/O and updater integration
remain local to each stack. These changes have native regression coverage;
hardware acceptance is a separate step.

## Lighting

The factory slider uses 0–255. Upstream QMK already scales that protocol range to the configured LED maximum of 80, so no increase in LED current or change to the original JSON is needed. Effects 0–44 keep their upstream ordering. The factory “Close All” entry 45 is routed to the normal disable command, instead of selecting this fork's recorded-RGB effect. Readback of a disabled matrix is the equivalent “All Off” value 0.

This restores the JSON's control contract; it does not establish identical animation appearance, recorded-lighting behavior, radio behavior, or complete factory firmware equivalence on hardware.

## Existing saved mappings

Loading a definition does not rewrite EEPROM. The earlier project firmware used conflicting meanings at the same numbers; do not blindly reuse its saved custom keys, and do not automatically guess whether unversioned EEPROM came from stock or that port.

Before an eventual update, export each half's mappings if needed. For a VIA export made using the earlier project build, use the offline converter in `tools/keymap-migration/` exactly once. After flashing, initialize the compiled defaults with Fn+Backspace held for three seconds, then import the converted VIA export. Stock exports already have stock numbering and must **not** be converted. A raw matrix recovery backup can be converted too, but remains a raw dump and is not directly importable by VIA.

No automatic device migration, EEPROM reset, or flashing is performed by the build or converter. The separate right-half bootloader recovery problem still requires its documented recovery procedure.

## Validation

Host tests exercise numeric custom slots, all device/physical-selector combinations, the six shortcut mappings, original lighting packet routing, and the existing mode/role/wireless regression cases. Python tests check migration of layers and encoder maps, preservation of unrelated metadata/macros, invalid-input rejection, and protection against overwriting an input or output file. Both QMK keymaps must compile before delivery; hardware acceptance remains separate.

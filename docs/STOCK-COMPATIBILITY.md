# Original VIA definition compatibility

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

The explicit enum in `firmware/qmk-common/keyboards/zuoya/gmk70/stock_keycodes.h` preserves gaps instead of renumbering later keys. Unused slots remain unhandled. Bluetooth 4/5 and USB now have handlers; mode selection still respects the physical switch. The radio module's actual support for five simultaneous stored Bluetooth pairings remains a hardware-validation item. Wireless selection/pairing retains the three-second hold, and unrelated key events no longer replace the pending pairing key.

## Lighting

The factory slider uses 0–255. Upstream QMK already scales that protocol range to the configured LED maximum of 80, so no increase in LED current or change to the original JSON is needed. Effects 0–44 keep their upstream ordering. The factory “Close All” entry 45 is routed to the normal disable command, instead of selecting this fork's recorded-RGB effect. Readback of a disabled matrix is the equivalent “All Off” value 0.

This restores the JSON's control contract; it does not establish identical animation appearance, recorded-lighting behavior, radio behavior, or complete factory firmware equivalence on hardware.

## Existing saved mappings

Loading a definition does not rewrite EEPROM. The earlier project firmware used conflicting meanings at the same numbers; do not blindly reuse its saved custom keys, and do not automatically guess whether unversioned EEPROM came from stock or that port.

Before an eventual update, export each half's mappings if needed. For a VIA export made using the earlier project build, use the offline converter in `tools/keymap-migration/` exactly once. After flashing, initialize the compiled defaults with Fn+Backspace held for three seconds, then import the converted VIA export. Stock exports already have stock numbering and must **not** be converted. A raw matrix recovery backup can be converted too, but remains a raw dump and is not directly importable by VIA.

No automatic device migration, EEPROM reset, or flashing is performed by the build or converter. The separate right-half bootloader recovery problem still requires its documented recovery procedure.

## Validation

Host tests exercise numeric custom slots, all device/physical-selector combinations, the six shortcut mappings, original lighting packet routing, and the existing mode/role/wireless regression cases. Python tests check migration of layers and encoder maps, preservation of unrelated metadata/macros, invalid-input rejection, and protection against overwriting an input or output file. Both QMK keymaps must compile before delivery; hardware acceptance remains separate.

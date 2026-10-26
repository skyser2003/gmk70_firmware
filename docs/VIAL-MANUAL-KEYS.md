# GMK70 manual keys in Vial

The manufacturer's bilingual GMK70 leaflet is available from
[its distribution PDF](https://ae01.alicdn.com/kf/S82ca1adeebf04bdfb241731b12233d4ei.pdf)
and the [Manuals+ mirror](https://manuals.plus/m/afce334373cd4a880f56d8db405f46f6028be78bc31c28f94df390d428666f4c.pdf).
This reference covers its function shortcuts and layer legend. It does not
replace your saved keymap or assume the leaflet's shortcuts are still assigned.

## Keyboard-specific controls

Select a position in Vial, then choose the named control in the **User** tab.
Battery/charging controls remain held queries. Device controls retain their
short-press selection and three-second pairing hold; the hardware selector must
be in the corresponding mode. Fn is part of the manual's shortcut, not something
to press again when using the remapped control.

| Manual shortcut | User-tab label | Stored keycode |
| --- | --- | --- |
| Fn+Q | BT DEV1 | `0x7E00` |
| Fn+W | BT DEV2 | `0x7E01` |
| Fn+E | BT DEV3 | `0x7E02` |
| Fn+R | 2.4G | `0x7E05` |
| USB Mode in layer legend | USB | `0x7E06` |
| Fn+left Space | Show Charging | `0x7E07` |
| Fn+Backslash | Logo Effects | `0x7E09` |
| Fn+left Ctrl | Num/F Toggle | `0x7E1A` |
| Fn+B | Show Battery | `0x7E1B` |
| Fn+A | Windows Mode | `0x7E22` |
| Fn+S | Mac Mode | `0x7E23` |
| Fn+Win | Windows Lock | `0x7E24` |
| Fn+X | RGB On/Off | `0x7E25` |
| Fn+Tab | RGB Effect | `0x7E26` |
| Fn+Up | Brightness + | `0x7E27` |
| Fn+Down | Brightness - | `0x7E28` |
| Fn+Right | Effect Speed + | `0x7E29` |
| Fn+Left | Effect Speed - | `0x7E2A` |
| Fn+right bracket | Hue + | `0x7E2B` |
| Fn+left bracket | Hue - | `0x7E2C` |
| Fn+quote | Saturation + | `0x7E2D` |
| Fn+semicolon | Saturation - | `0x7E2E` |

The new slots `CUSTOM(34)` through `CUSTOM(46)` are Vial aliases for existing
mode, lock and RGB handlers. They retain the existing persistence, split-lighting
and RGB-processing behavior. Standard lighting keys in Vial remain usable too.
Stock slots `0..33`, their reserved gaps and the original VIA definition retain
their numeric contract. No EEPROM migration or default-map rewrite is part of
this change. Existing saved standard keys keep their standard Vial labels;
assigning a named User control uses its new alias.

Windows/Mac mode selects the existing base layout, including the stock modifier
arrangement. Windows Lock toggles the existing GUI-key lock. Num/F Toggle is
the board's existing number-row/function behavior, not another Vial layer.

## Standard keys already in Vial

The remaining manual functions use Vial's existing **Basic**, **Layers**, and
**App, Media and Mouse** tabs. They do not need new custom codes:

| Manual shortcut | Assignable Vial/QMK key |
| --- | --- |
| Fn+1 through Fn+0, Fn+minus, Fn+equals | `KC_F1` through `KC_F12` |
| Fn+F1 | `KC_MSEL` (media player) |
| Fn+F2 / Fn+F3 | `KC_VOLD` / `KC_VOLU` |
| Fn+F4 | `KC_MUTE` |
| Fn+F5 | `KC_MSTP` |
| Fn+F6 / Fn+F8 | `KC_MPRV` / `KC_MNXT` |
| Fn+F7 | `KC_MPLY` |
| Fn+F9 | `KC_MAIL` |
| Fn+F10 | `KC_WHOM` (browser home) |
| Fn+F11 | `KC_CALC` |
| Fn+F12 | `KC_WSCH` (browser search) |
| Fn+Y / Fn+U / Fn+I | `KC_PSCR` / `KC_SCRL` / `KC_PAUS` |
| Fn+H / Fn+J | `KC_HOME` / `KC_END` |
| Fn+K / Fn+L | `KC_PGUP` / `KC_PGDN` |
| Fn+Delete | `KC_INS` |
| Windows Fn layer | `MO(1)` |
| Mac Fn layer | `MO(3)` |
| MO(2) in the layer legend | `MO(2)` |

Application-launch and browser keys depend on the host OS. Basic keys, modifiers,
arrows and macro assignments also remain available in their ordinary Vial tabs.

## Hardware and automatic operations

The physical BT/USB/2.4G selector, power switch, USB insertion and power-on DFU
procedures are hardware operations. The leaflet's automatic idle sleep and
low-battery indication are not assignable key commands. This change does not
alter them or the radio firmware. The existing software Bootloader control in
Vial is separate from the leaflet's physical recovery sequence.

The definition is embedded in main-MCU Vial firmware, so both ordinary Vial and
Vial-updater builds include the new picker labels. No Vial client changes or
JSON sideloading are required after installing a matching build.

# Experimental Vial port

`make setup-vial` initializes the independently pinned Vial fork;
`make build-vial` builds the normal Vial application and a firmware ZIP;
`make build-vial-updater` builds the resident-compatible split updater package.
The port reuses
`firmware/qmk-common/` for the board, keymap, radio and power logic. Its adapter,
UID, physical unlock configuration and embedded layout are maintained here.

`compat/platform_rules.mk` selects Vial's integration independently of VIA.
`compat/rgb_integration.c` rejects RGB writes to the other half before Vial's
global-to-local LED conversion, keeping Bluetooth indicators on Q/W/E and
preserving the right half's normal lighting. Board/radio sources stay shared.

See [the port report](../../docs/VIAL-PORT.md) for protocols, EEPROM separation,
software validation and remaining work. Live QMK settings, tap dances, combos,
key overrides, Caps Word, Layer Lock, Repeat/Alternate Repeat, extended macros,
and VialRGB/direct LED control are enabled. Dynamic tables provide 32 entries
each; 16 macros share 2123 bytes. Auto Shift and Flow Tap start off and can be
configured in Vial. The updater relays from left USB to the linked right half,
retains native Vial unlock for writes, and excludes EEPROM queries and logging.
Use the updater package's `application.bin` with an existing compatible resident
installer; the normal image cannot be submitted to it. Hardware validation remains
pending.

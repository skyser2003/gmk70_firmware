# Experimental Vial port

`make setup-vial` initializes the independently pinned Vial fork;
`make build-vial` builds the normal Vial application and a firmware ZIP. The port reuses
`firmware/qmk-common/` for the board, keymap, radio and power logic. Its adapter,
UID, physical unlock configuration and embedded layout are maintained here.

See [the port report](../../docs/VIAL-PORT.md) for protocols, EEPROM separation,
software validation and remaining work. Live QMK settings, tap dances, combos,
key overrides, Caps Word, Layer Lock, Repeat/Alternate Repeat, extended macros,
and VialRGB/direct LED control are enabled. Dynamic tables provide 32 entries
each; 16 macros share 2123 bytes. Auto Shift and Flow Tap start off and can be
configured in Vial. Hardware validation and resident-updater integration remain
pending. This image is not an application for the installed split updater.

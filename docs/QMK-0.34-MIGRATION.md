# QMK 0.34.4 integration

The official QMK dependency is pinned to tag `0.34.4`, commit `08c662f286ddfd12a985f57b584b02eca5af0ae6`. Builds do not resolve a moving tag or branch. QMK and its required libraries remain unmodified; maintained changes are confined to the GMK70 integration and project tooling.

## Compatibility changes

- Board metadata supplies the nonempty URL required by the current schema.
- GPIO calls use the current `gpio_*` API. The radio explicitly selects UART3, TX C10, RX C11 and alternate function 7 using `UART_*` configuration names. This also keeps the low-power wake path on the radio RX pin. The split still uses UART1 A9/A10.
- Radio transport selects report protocol through `usb_device_state_set_protocol()` instead of the removed `keyboard_protocol` global.
- Keymap settings use the current pointer-based `eeconfig` API. Recorded RGB data uses the public user-datablock API with offsets, instead of private EEPROM address macros. The 720-byte recording area and following 20-byte custom area retain their allocation within the 4 KiB logical EEPROM.
- Default lighting bindings use `RM_*` matrix keycodes. Existing saved `UG_*` lighting bindings (wire values `0x7820` through `0x782A`) are translated before board handling and then passed to QMK's matrix handler. This preserves lighting control when importing earlier saved maps.
- Explicit vendor custom-key values, reserved gaps and the original JSON remain unchanged. The stock “Close All” effect 45 adapter is in `firmware/qmk-via/compat/`. Enabled animation ordering is unchanged; the new upstream starlight-smooth animation is not enabled.
- VIA uses upstream protocol `0x000D` and its keycode-version response, replacing `0x000C`. The definition's version 3 format is separate from this wire-protocol number. Actual VIA host interaction still requires hardware validation.

QMK's EEPROM core layout and validity marker changed in this upgrade. A future flash of this firmware will cause QMK to initialize incompatible saved settings. Export mappings before updating; do not restore a raw EEPROM image from the old firmware. Reimport a VIA saved-keymap export after initialization. The older custom-keycode migration utility is only for the earlier compact-numbering project firmware, as described in its README; it is not needed merely because QMK was upgraded.

## Build checks

Builds verify BIN package checksums and dependency pins. Repository tests cover
source origins, dependency cleanliness, variant isolation and safe cleanup.
Native regressions exercise radio reports, role selection, stock/legacy
keycodes and VIA lighting routes. Run `make test`, `make check-tools` and the
affected firmware builds after loading `scripts/project.sh`.

Build/test commands never access the keyboard. Physical radio, sleep, split,
VIA and lighting checks follow [hardware acceptance](HARDWARE-VALIDATION.md).

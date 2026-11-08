# Right-half bootloader recovery

## Software bootloader command

Current VIA release and debug firmware implement the deferred stock command
`0x0B`. See [debug/release behavior](DEBUG-RELEASE.md) for its packet contract.
Build/test commands never send it to hardware. Older firmware can lack this
handler, requiring physical recovery.

## Confirmed firmware regression

The first fork build introduced an `is_keyboard_master()` override that returned only the B9 handedness signal. This preserved a left master without USB for wireless operation but incorrectly kept the right half in the slave role even when directly connected to a PC. QMK's key processing depends on that role, so a VIA-assigned `QK_BOOT` key could not execute locally on the right half.

Stock V13's role selector at `0x08004BEC` first reads GPIOA input (`0x40000010`) and tests bit 7, the USB insertion signal. If A7 is high it returns true. Otherwise it configures and reads B9 at GPIOB input (`0x40000410`). This matches the published GMK70 board source. The four cases were also checked by executing the stock Thumb routine with simulated GPIO registers and its GPIO-configuration call stubbed:

| Direct USB (A7) | Left hand (B9) | Master |
| --- | --- | --- |
| No | No | No |
| No | Yes | Yes |
| Yes | No | Yes |
| Yes | Yes | Yes |

`split_role.c` restores that priority. `tests/qmk/split_role_test.c` compiles this production function against a GPIO fixture; the previous implementation fails the right-half USB case. This test does not establish the physical state of A7 on every PCB or validate connecting both halves to separate USB hosts.

Binary reference: the supplied stock V13 decoded image has SHA-256 `857a669c1fc739cd18ccd9a5526e80db10b150fbdea344b36031105e72e661f3`.

## Mapped Reset versus Bootmagic

- A VIA Reset/Bootloader key is `QK_BOOT` (`0x7C00`). It must be read from the connected half's active keymap and processed by the firmware. In this board's inherited handler it also invalidates EEPROM settings before jumping to the bootloader.
- Bootmagic checks a fixed physical matrix location during startup, independent of VIA assignments. The current port specifies only row 0, column 0; it does not define a separate right-hand Bootmagic key.
- Each half has its own EEPROM. A key assigned through the normal left-master connection is not automatically copied into the right half's standalone keymap. After restoring direct USB operation, inspect/configure the right half separately if its Reset assignment is absent.

Do not assume that holding any key labelled Reset in VIA is equivalent to Bootmagic. Also preserve VIA settings before using reset paths that invalidate EEPROM.

## Getting an already-flashed right half into the bootloader

The supplied vendor flashing instructions describe a separate right-half pad procedure. With the right half disconnected from power, set its small internal switch to the lower position described in that document, bridge the two specified small holes beside the large spacebar-switch mounting hole, and connect that half directly to the PC by USB. Use only the pads identified by the vendor instructions; disconnect the inter-half cable for this recovery attempt. Return the internal switch to its normal upper position after the update.

This is the factory-documented recovery method. After installing compatible firmware, test the right half directly over USB, confirm its own stored Reset assignment, then test bootloader entry using that assignment.

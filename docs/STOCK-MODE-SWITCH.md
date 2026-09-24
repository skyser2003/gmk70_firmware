# GMK70 stock V13 mode-switch evidence

The inherited uk-taniyama port assigns Bluetooth to C14 and 2.4 GHz to C15. Those assignments are reversed relative to the supplied ZUOYA GMK70 V13 firmware. The correction swaps the two definitions in `firmware/qmk-common/keyboards/zuoya/gmk70/wls/wls.h`; it preserves the existing keycodes, physical-mode restrictions, and three-second pairing behavior.

## Binary identity and method

- Input: `ZUOYA GMK70 V13.hex`, SHA-256 `5924d7516155aa499fdc65ea5168b9d7e62bd947cb8e31f668362224836d026e`.
- Decoded application: 79,976 bytes at `0x08000000`, SHA-256 `857a669c1fc739cd18ccd9a5526e80db10b150fbdea344b36031105e72e661f3`.
- Thumb disassembly locates the mode detector at `0x08002404`. The binary is stripped, so this function name is inferred from its GPIO reads, mode outputs, and callers.
- The literal at `0x080024B4` is `0x40000800` (GPIOC); the function reads its input register at offset `0x10` and tests bits 15 and 14.
- The stock short-press and long-press wireless handlers call this detector. It writes `1` for USB, `2` for Bluetooth, `3` for 2.4 GHz, and `0` for neither recognized position.
- As a cross-check, the actual stock Thumb instructions were executed with Unicorn for all four pin combinations. GPIO input was simulated and `timer_read32` at `0x0800E518` was stubbed to return 100. This validates the decoding of the binary, not physical radio operation.

## Recovered truth table

| C14 input | C15 input | Stock mode | Inherited port |
| --- | --- | --- | --- |
| High | High | USB | USB |
| High | Low | Bluetooth | 2.4 GHz |
| Low | High | 2.4 GHz | Bluetooth |
| Low | Low | None | No recognized position |

For example, at `0x08002428` the stock code tests bit 15 for low, then bit 14 for high at `0x0800242E`. The taken branch reaches `0x08002498` (internal Bluetooth selector `1`), followed by the output `2` at `0x08002480`. The corresponding 2.4 GHz output is written at `0x080024A4`.

The published Panda GMK70 source comments out the mode-pin definitions, but the supplied stock binary demonstrably reads these pins. Removing mode detection to match that source would therefore discard behavior present in the binary.

## Verification and device follow-up

`tests/qmk/wireless_mode_test.c` checks the production pin predicates against the recovered truth table, including USB and the unrecognized state. It fails on Bluetooth input with the old definitions and passes after the swap. The normal wireless tests and both VIA/default firmware builds must also pass.

After installing the corrected firmware, confirm Bluetooth slot selection and pairing with the switch in BT, and receiver operation/pairing with the switch in 2.4G. The middle USB position still blocks wireless shortcuts. A short press of the already-selected device does not necessarily restart its indicator; holding the chord for three seconds requests pairing. These physical checks remain pending. No device was flashed as part of this analysis.

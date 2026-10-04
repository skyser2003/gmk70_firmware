# Debug and release firmware

From October 3, 2026, `make build` produces two QMK/VIA distributions. Both share the official pinned QMK framework, GMK70 board/radio sources, compiled keymap, original VIA definition, battery-key fixes, and 5 ms `sym_defer_g` debounce.

| Build | Command | Firmware | Extra features |
| --- | --- | --- | --- |
| Release | `make build-release` | `artifacts/qmk/release/zuoya_gmk70_release.bin` or `.hex` | None; remote bootloader request unsupported |
| Debug | `make build-debug` | `artifacts/qmk/debug/zuoya_gmk70_debug.bin` or `.hex` | Existing split/sleep RAM logger and direct-USB bootloader command |

Debug retains the same compiler optimization level as release. It is a feature selection, not a global switch to slower unoptimized code. Only its rules enable `GMK70_SPLIT_DIAGNOSTICS` and `GMK70_DEBUG_USB_BOOTLOADER`. Normal key handling, mode selection, sleep policy, and LED output are shared. The original VIA JSON stays unchanged.

The old `build-qmk-via`/`build-diagnostic` Make targets and `scripts/build.sh qmk via`/`via_diagnostic` arguments alias the new names. They write only the new artifact paths. The optional `build-default` fixed-keymap target remains available for development, without VIA or debug commands; it is not a third distribution produced by `make build` or CI. Legacy generated trees remain cleanable. Old artifact directories are retained as historical outputs.

## Direct USB bootloader command

After debug firmware has been installed, send a 32-byte VIA raw-HID payload:

```text
0B 00 00 00 ... 00
```

Byte 0 is VIA's existing `id_bootloader_jump`; bytes 1–31 must all be zero. The interface is the directly connected GMK70 (`342D:E491`, usage page `FF60`, usage `61`). A host API requiring a HID report-ID prefix sends a leading zero outside this 32-byte payload.

Acceptance requires the connected half to be the USB master, USB to be active, and the selected transport to be wired. A radio-delivered `0x0B` is rejected at the radio raw-HID entry point even when the selected transport happens to be USB. The split protocol has no remote bootloader forwarding; connect each half directly to update it.

On acceptance, the firmware echoes the request, waits 100 ms to allow the USB acknowledgement to leave, then calls upstream `reset_keyboard()`. That function clears held keyboard reports, performs normal QMK shutdown, and enters the WB32 bootloader. Its configured shutdown delay can add to the 100 ms acknowledgement interval. No EEPROM reset/invalidation, keymap rewrite, polling, periodic timer, or retry is added. This does not invoke the board's physical `QK_BOOT` handler, which still has its inherited EEPROM-invalidating behavior.

A full-sized request with nonzero padding or an invalid connection state receives `FF` in byte 0 and does not reset. Release's unchanged upstream VIA dispatcher returns `FF` for `0x0B`. Malformed packet sizes are not accepted by the new handler. Existing getter tools do not send this command.

The command can only be used after installing debug firmware once through an already-working bootloader-entry method. Installing release removes remote bootloader entry again; the ordinary physical bootloader key remains available. USB drivers and actual hardware enumeration still need a separate device test.

## Verification and packaging

Native tests compile the actual VIA adapter and radio raw-HID path in release/debug configurations. They check command exclusion in release, valid debug dispatch, acknowledgement-before-reset ordering, all packet padding bytes, invalid lengths/connection states, and rejection of radio-delivered requests. Reset, delay and USB functions are mocked: tests never access the keyboard. Existing lighting, battery, report-routing and diagnostic tests remain required.

Builds inspect the compiled adapter and flags to reject a release accidentally containing the remote reset call or debug flags. Each package includes BIN/HEX (identical application payload), ELF/MAP, compiler flags, checksums and a full source archive. CI distributes uniquely named release/debug archives so their manifests cannot collide in GitHub's flat asset namespace.

The pre-change sources and artifacts are retained at `artifacts/backups/2026-10-03-before-debug-release/`. Builds/tests do not reset or flash connected devices.

On October 3, the user separately authorized installing the NONE-wake correction on each half. Both acknowledged the direct-USB command and enumerated in WB32 DFU. The updater wrote the debug image without disabling read protection or resetting EEPROM; a complete 69,468-byte readback from each half matched SHA-256 `a82d93e789e54a3e1bae885573c11dab3a6476d90c2a5e336091a0af08afd89b`. Both applications re-enumerated, and all 360 matrix assignments on each half matched their pre-update backups. These checks establish software bootloader entry and installation on this keyboard, not general wireless stability. Logs, firmware readbacks, mapping backups and the installation record are under `artifacts/recovery/2026-10-03-*none-wake*`.

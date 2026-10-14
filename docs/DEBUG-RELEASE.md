# Debug and release firmware

`make build` produces two QMK/VIA distributions. Both share the official pinned QMK framework, reconstructed V15 board/radio behavior, compiled keymap, original VIA definition, and 5 ms `sym_defer_g` debounce. Vial uses the same debounce setting.

| Build | Command | Firmware | Extra features |
| --- | --- | --- | --- |
| Release | `make build-release` | `artifacts/qmk/release/zuoya_gmk70_release.bin` or `.hex` | Stock ordinary VIA commands; no diagnostics |
| Debug | `make build-debug` | `artifacts/qmk/debug/zuoya_gmk70_debug.bin` or `.hex` | Stock ordinary VIA commands plus split/sleep RAM logger |

Debug retains the same compiler optimization level as release. It is a feature selection, not a global switch to slower unoptimized code. Only its rules enable `GMK70_SPLIT_DIAGNOSTICS` and `GMK70_DEBUG_USB_BOOTLOADER`. Normal key handling, mode selection, sleep policy, and LED output are shared. The original VIA JSON stays unchanged.

The old `build-qmk-via`/`build-diagnostic` Make targets and `scripts/build.sh qmk via`/`via_diagnostic` arguments alias the new names. They write only the new artifact paths. The optional `build-default` fixed-keymap target remains available for development, without VIA or debug commands; it is not a third distribution produced by `make build` or CI. Legacy generated trees remain cleanable. Old artifact directories are retained as historical outputs.

## Stock bootloader command

Both current VIA distributions implement the recovered V15 request: a 32-byte raw-HID packet with byte 0 equal to `0x0B`. Stock accepts the other bytes without the old zero-padding/USB-only gate. The handler sets the pending bootloader flag and replies with byte 1 equal to 1. The normal master board task later invokes QMK's reset-to-bootloader path. Radio-delivered ordinary requests are supported too.

The previous debug-only command, its 100 ms wait and release rejection are superseded by [the stock reconstruction](STOCK-V15-RECONSTRUCTION.md). Diagnostic recording and the optional split updater remain debug features; updater commands keep their separate direct-USB gates. No build/test command sends the bootloader request to hardware.

## Verification and packaging

Native tests compile the actual VIA adapter and radio raw-HID path in release/debug configurations. They check ordinary command support in both distributions, request/echo ordering, accepted stock padding, invalid lengths and radio delivery. Immediate reset is forbidden in the handler fixture. Existing lighting, battery, report-routing and diagnostic tests remain required. Tests never access a keyboard.

Builds inspect the compiled adapter and flags to require the deferred stock bootloader request and reject debug flags in release. Each package includes BIN/HEX (identical application payload), ELF/MAP, compiler flags, checksums and a full source archive. CI distributes uniquely named release/debug archives so their manifests cannot collide in GitHub's flat asset namespace.

The pre-change sources and artifacts are retained at `artifacts/backups/2026-10-03-before-debug-release/`. Builds/tests do not reset or flash connected devices.

On October 3, the user separately authorized installing the NONE-wake correction on each half. Both acknowledged the direct-USB command and enumerated in WB32 DFU. The updater wrote the debug image without disabling read protection or resetting EEPROM; a complete 69,468-byte readback from each half matched SHA-256 `a82d93e789e54a3e1bae885573c11dab3a6476d90c2a5e336091a0af08afd89b`. Both applications re-enumerated, and all 360 matrix assignments on each half matched their pre-update backups. These checks establish software bootloader entry and installation on this keyboard, not general wireless stability. Logs, firmware readbacks, mapping backups and the installation record are under `artifacts/recovery/2026-10-03-*none-wake*`.

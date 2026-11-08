# Debug and release firmware

`make build` produces two QMK/VIA distributions. Both share the official pinned QMK framework, reconstructed V15 board/radio behavior, compiled keymap, original VIA definition, and 5 ms `sym_defer_g` debounce. Vial uses the same debounce setting.

| Build | Command | Firmware | Extra features |
| --- | --- | --- | --- |
| Release | `make build-release` | `artifacts/qmk/release/zuoya_gmk70_release.bin` | Stock ordinary VIA commands; no diagnostics |
| Debug | `make build-debug` | `artifacts/qmk/debug/zuoya_gmk70_debug.bin` | Stock ordinary VIA commands plus split/sleep RAM logger |

Debug retains the same compiler optimization level as release. It is a feature selection, not a global switch to slower unoptimized code. Only its rules enable `GMK70_SPLIT_DIAGNOSTICS` and `GMK70_DEBUG_USB_BOOTLOADER`. Normal key handling, mode selection, sleep policy, and LED output are shared. The original VIA JSON stays unchanged.

The old `build-qmk-via`/`build-diagnostic` Make targets and `scripts/build.sh qmk via`/`via_diagnostic` arguments alias the new names. They write only the new artifact paths. The optional `build-default` fixed-keymap target remains available for development, without VIA or debug commands; it is not a third distribution produced by `make build` or CI. Legacy generated trees remain cleanable. Old artifact directories are retained as historical outputs.

## Stock bootloader command

Both current VIA distributions implement the recovered V15 request: a 32-byte raw-HID packet with byte 0 equal to `0x0B`. Stock accepts the other bytes without the old zero-padding/USB-only gate. The handler sets the pending bootloader flag and replies with byte 1 equal to 1. The normal master board task later invokes QMK's reset-to-bootloader path. Radio-delivered ordinary requests are supported too.

The previous debug-only command, its 100 ms wait and release rejection are superseded by [the stock reconstruction](STOCK-V15-RECONSTRUCTION.md). Diagnostic recording and the optional split updater remain debug features; updater commands keep their separate direct-USB gates. No build/test command sends the bootloader request to hardware.

## Verification and packaging

Native tests compile the actual VIA adapter and radio raw-HID path in release/debug configurations. They check ordinary command support in both distributions, request/echo ordering, accepted stock padding, invalid lengths and radio delivery. Immediate reset is forbidden in the handler fixture. Existing lighting, battery, report-routing and diagnostic tests remain required. Tests never access a keyboard.

Builds inspect the compiled adapter and flags to require the deferred stock bootloader request and reject debug flags in release. Each package includes BIN firmware, ELF/MAP, compiler flags, checksums and a full source archive. CI distributes uniquely named release/debug archives so their manifests cannot collide in GitHub's flat asset namespace.

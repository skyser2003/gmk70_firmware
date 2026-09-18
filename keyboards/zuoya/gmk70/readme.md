# ZUOYA GMK70

Target: `zuoya/gmk70`, WB32FQ95, WB32 DFU. Build both halves from the same target; the B9 hardware pin determines the role. Physical compatibility and all wireless/sleep behavior need testing on the actual keyboard revision.

In the parent `gmk70_firmware` project, use `make build` for VIA or `make build-default` for the fixed keymap. The parent README documents the pinned toolchain, source provenance, regression tests, and pending hardware validation.

When the board and shared `keyboards/linker/wireless` sources have been installed in the pinned QMK checkout, the standard QMK target is `make zuoya/gmk70:via` or `make zuoya/gmk70:default`.

The provided `via.json` matches this fork's custom keycode order. Bootmagic uses matrix position `[0,0]` (encoder press); the compiled keymap also binds Fn+encoder press to `QK_BOOT`. This project has not been flashed or tested on hardware during the build work.

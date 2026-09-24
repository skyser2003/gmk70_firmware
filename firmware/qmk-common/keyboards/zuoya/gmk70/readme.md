# ZUOYA GMK70

Target `zuoya/gmk70`, WB32FQ95 with WB32 DFU. This folder is the authoritative board source under `firmware/qmk-common/`. Builds compose it with the pinned official QMK dependency. Run `make build` for VIA or `make build-default` for the fixed keymap from the repository root.

Use `definitions/via/ZUOYA_GMK70.json` from the repository root. Stock custom keycodes are explicit in `stock_keycodes.h`. See the root compatibility and recovery documents before flashing. Bootmagic currently checks matrix `[0,0]` (encoder press), not Escape; the compiled Fn+encoder binding is a runtime bootloader key. Each half's saved mapping can override compiled defaults. Direct USB now gives either half the master role, but an older installed build still needs recovery before that fix can be installed.

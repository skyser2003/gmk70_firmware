# Official QMK / VIA additions

`keymaps/release/` adds VIA remapping to the shared GMK70 board. `keymaps/debug/` shares its configuration and adds the split logger and direct-USB bootloader command. `compat/stock_via.c` preserves the original definition's lighting-off command in both builds; the remote bootloader branch is compiled only for debug. These are composed with `firmware/qmk-common/` and the pinned official QMK dependency into `.work/qmk/<variant>/`.

`make build` produces release and debug; `make build-release` and `make build-debug` select one. The optional fixed base keymap remains available with `make build-default`. Shared hardware/radio sources belong in `qmk-common`. Vial will use its own fork and additions under `qmk-vial`. See `docs/DEBUG-RELEASE.md` for command boundaries and artifact paths.

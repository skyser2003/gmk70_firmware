# Official QMK / VIA additions

`keymaps/via/` adds VIA remapping to the shared GMK70 board. `compat/stock_via.c` preserves the original definition's lighting-off command. Both are composed with `firmware/qmk-common/` and the pinned official QMK dependency into `.work/qmk/<variant>/`.

Build the fixed base keymap with `make build-default`, or the VIA keymap with `make build`. Shared hardware/radio sources belong in `qmk-common`. Vial will use its own fork and additions under `qmk-vial`.

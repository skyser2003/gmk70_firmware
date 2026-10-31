# ZMK idle matrix scanning: offline validation

Built on main `6a4524c`, retaining its shared hardware safeguards and default-on
NKRO. No keyboard, settings, radio or resident was accessed or changed.

`make test`, `make test-zmk` and `make check-tools` pass. Tooling checks include
90 repository, 23 keymap-migration, 16 battery-diagnostic, 187 updater and 34 ZMK
Python cases; one optional updater case is skipped. Native tests additionally
exercise the actual GPIO/EXTI and wired matrix implementations with simulated
MMIO, including both roles, 1,000 quiet calls without another GPIO sweep,
press/release/held-key debounce, queued IRQs, arming races, ownership conflicts
and polling fallback. Existing USB-power and peer/disconnect tests pass.

All nine supported ZMK variants compile and pass their image checks. The
canonical `wired-studio-updater` image is 82,712 bytes with 27,928 bytes of
reserved RAM and 616 unallocated bytes. Against a rebuild of the pulled base,
the change adds 1,328 application bytes and 136 RAM bytes. No thread or stack
reservation was added. The installed user's older image is a separate baseline.

The generated ARM interrupt table was checked: EXTI IRQs 6/7/8/9/10/16/33 all
dispatch to the real driver with the correct individual/group GPIO masks.
Every applicable package retains resident ABI 1, padded resident CRC32
`1770733249` and the exact resident SHA256. Dependencies, source pins, VIA
definition, saved-settings schema and permanent no-watchdog policy are unchanged.

Sizes, hashes, compiler identity and IRQ evidence are in [result.json](result.json).
Artifacts are under `artifacts/zmk/<variant>/`; the universal package is
`artifacts/zmk/wired-studio-updater/`.

Hardware interrupt delivery, first-key behavior on both halves, typing, RGB,
USB/Studio/updater continuity and current consumption still need physical
validation. This change does not enable system STOP or radio sleep. Maintenance
polling continues while local GPIO sweeps are idle, so no whole-system sleep or
battery-runtime improvement is claimed from these offline checks.

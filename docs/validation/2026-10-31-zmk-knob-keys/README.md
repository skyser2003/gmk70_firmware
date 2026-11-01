# GMK70 Studio knob keys: offline validation

Adds appended positions 72 (counterclockwise) and 73 (clockwise) on base
`a2f9efe`, preserving all original 72 default bindings on all four layers.
Studio physical geometry keeps the original keys unchanged and adds two
ordinary editable slots below them. No upstream/client or VIA definition edit.

Passed `make test`, `make test-zmk` and `make check-tools`, including 35 ZMK
Python and 189 updater-tool tests (one optional updater case skipped). Focused
native tests compile the actual encoder, GPIO/wired adapter and kscan code;
platform registers, GPIO callbacks and time are modeled. Cases include bounce,
partial and opposite turns, overflow, idle matrix operation, IRQ conflicts and
polling fallback, 5 ms pulses/gaps, disable/mode releases and timer wraparound.

The existing four-layer settings fixture gains eight rotation assignments,
retains all original bindings, reloads them and exercises further compactions.
Framed Studio tool fixtures test full/selective 72-key imports on a 74-key map,
preservation of all eight knob bindings and full restoration after a failure.

All seven affected ZMK ARM variants compile and pass image checks. The normal
universal Studio updater is 83,672 bytes, reserving 28,136 RAM bytes and leaving
408 bytes unallocated. This adds 960 application bytes and 208 RAM bytes
relative to the base: 128 bytes are the eight new keymap bindings; other state,
keymap bookkeeping and the expanded settings index account for the rest.
No new thread, stack reservation or general heap is added.

Packages retain the exact ABI-1 resident (padded CRC32 `1770733249`), updater
gates, settings partitions and permanent no-watchdog policy. Checks, hashes,
sizes and compiler identity are in [result.json](result.json). Canonical output
is `artifacts/zmk/wired-studio-updater/`.

No physical device was accessed, flashed or reset. Hardware direction/detents,
Studio editing/saving/reloading, USB/Bluetooth/dongle receipt and runtime stack
watermarks remain unmeasured. The unchanged existing low-power limits still
apply; this feature does not add system deep sleep.

# Exact GMK70 ZMK RAM extent

The WB32 device tree already admits SRAM `[0x20000040, 0x20006fc0)`:
28,544 bytes, excluding 64 bytes at either end of the original 28 KiB window.
Pinned Zephyr 4.1's Cortex-M linker template instead uses
`CONFIG_SRAM_SIZE * 1K`, truncating that extent to 27,648 bytes. The resulting
896-byte gap was available hardware memory outside the old linker region.

The WB32 SoC CMake integration now generates one linker input under the build
directory from the unchanged pinned Cortex-M template. It replaces exactly its
single `RAM_SIZE` definition with `DT_REG_SIZE(DT_CHOSEN(zephyr_sram))`. A changed
upstream definition stops configuration for review. There is no maintained copy
of the template, dependency patch, source-tree overlay or Python staging step.
Linker assertions retain the exact start/end, kernel extent and allocation bound.
The image checker independently requires those boundary symbols and checks ELF
load segments against both retained regions.

Kconfig's whole-KiB value remains 27. The current port has no general kernel or
runtime libc heap. Automatic libc heaps that calculate their size from that
rounded Kconfig value, and newlib's implicit SRAM limit, are rejected by the
adapter until they receive their own byte-accurate integration. Fixed sections,
buffers and stacks can use the full admitted linker region.

For the universal Studio image recorded in
[the installation report](validation/2026-10-30-zmk-universal/result.json):

| Measurement | Before | After |
| --- | ---: | ---: |
| Linker RAM capacity | 27,648 | 28,544 |
| Allocated RAM, including stacks | 27,576 | 27,576 |
| Unallocated linker space | 72 | 968 |

This build's application bytes remain identical to the recorded installed image:
79,624 bytes, SHA256
`1e3cfa64060fbbeb7832dd8f5545d45bdba1cd4c6c212cfb2d38bf633739a1d9`.
The correction changes linker capacity and ELF metadata; it does not make the
running image allocate or exercise the additional bytes. Reflashing this exact
binary would install the same application. Saved layers/bindings, stack sizes,
settings banks, resident updater ABI, and the permanent no-watchdog policy are
unchanged.

`tests/zmk/test_ram_extent.py` links native allocation fixtures: the previous
allocation fits both layouts, an additional 896 bytes fits only the corrected
one, the exact admitted extent fits, and a one-byte overflow fails. Separate
cases reject a shifted region, missing/rounded boundary symbols, changed linker
templates and incompatible automatic heaps. Real ZMK builds are additionally
checked by `firmware/zmk/tools/check_image.py`.

These are offline build/linker checks. No live USB test, new RAM-cell test,
power-cycle test or runtime stack watermark measurement is implied. A future
image that actually allocates the recovered tail still needs hardware validation.
The [validation record](validation/2026-10-31-zmk-ram-extent/result.json) contains
all nine build results, resident checksums and the unchanged RAM-object comparison.

# Offline Mac-layer and lighting validation

These are **uninstalled candidates**, built from product commit `4d46219`.
No device read/write/reset/reconnect occurred. Installed firmware, settings and
resident identities remain the [earlier Studio checkpoint](../2026-10-28-zmk-studio/README.md).

| Candidate | Application bytes | Static RAM / 27,648 |
| --- | ---: | ---: |
| LEFT Studio | 71,048 | 27,128 |
| Universal normal wired (RIGHT candidate) | 44,100 | 17,544 |
| Wired update-test | 44,124 | 17,568 |
| Staging qualification | 43,676 | 17,568 |

[Machine-readable results](result.json) record CRC/SHA identities, image/RAM bounds,
the preserved padded resident identity, checks and outstanding hardware work.
All four builds passed the real ARM ELF/image checker. Their artifact directories
are `artifacts/zmk/<variant>/`; frozen task evidence is kept under the private
recovery archive after review. The original installed images remain frozen in
their earlier recovery directories.

`make test` passed native suites, 7 migration tests and 88 repository tests.
`make test-zmk` passed all native suites and 12 Python tests. The updater uv
project passed 113 tests with one optional built-resident skip. Ruff and strict
mypy passed for the four ZMK tools and eight updater files; edited C formatting
and exact dependency verification passed. QMK/Vial maintained sources and the
immutable VIA definition were unchanged, so no QMK build variant was affected.

New regressions include all 288 maximum-width bindings plus metadata; v1-to-v2
compaction interrupted at every byte of each written page; DMA encoding, bounds,
GPIO setup, error/timeout/frozen-timer/clock-wrap cleanup and quiescence; exact
36/41 slicing; claimed-frame ownership; CRC/loss/reordering/sequence wrap;
superseded UART results; and replay of the one-shot off frame. Existing USB,
updater worker, watchdog, receiver, layout and serial-framing tests also passed.

The Studio candidate uses smaller main/RPC stacks and matched 128-byte receive
rings to fit native RGB's separate work queue. Static compiler stack frames were
inspected, but no physical stack watermark, LED waveform/power, USB/UART timing
or key/Studio migration was tested. Those remain installation gates. The
[restart report](../../ZMK-WARM-RESTART-ANALYSIS.md) adds evidence without a fix;
there is no automatic restart or new on-keyboard success claim.

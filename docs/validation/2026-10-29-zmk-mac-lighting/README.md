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
recovery archive `artifacts/recovery/2026-10-29-mac-lighting/`. The original installed images remain frozen in
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
or key/Studio migration was tested. Those remain qualification gates. The
[restart report](../../ZMK-WARM-RESTART-ANALYSIS.md) adds evidence without a fix;
there is no automatic restart or new on-keyboard success claim.

## Independent review and decisions

A fresh whole-feature review of `d27ab64..98292a2` found no actionable Critical,
Important or Minor issues and reran the native settings/lighting/pixel binaries
successfully. No code changes were requested. Subsequent edits correct manual
chapter references and clarify that hardware measurements happen during
supervised qualification.

The implementer retained the smaller Studio main/RPC stacks and matched receive
rings to fit the native RGB work queue. An incomplete stack analysis could miss
an overflow, so no installed-runtime margin is claimed. The full compiler audit
contains 1064 function records and three dynamic entries: the behavior-name VLA
is bounded to 64 bytes by pinned validation; the two kernel red-black-tree path
VLAs depend on tree height. Individual compiler frames are not a full-chain proof.

The review deliberately left physical LED waveform/order/color/power and DMA
bus behavior to hardware qualification; an incorrect electrical assumption could
produce bad lighting. It likewise left worst-case USB/UART latency, animation
throughput and watchdog progress under combined load unmeasured; a model cannot
establish runtime responsiveness. On-device Studio migration and saved bindings
remain untested in this candidate; host tests cannot prove the installed state.
These limits justify retaining the working installed image during unattended work.

Warm-restart root cause and recovery remain unresolved; the conditional timing
argument cannot establish watchdog retention or an actual failing phase. No
candidate is advertised as a restart repair. Frame sequencing assumes the existing
bounded, ordered UART transport: arbitrary historical packet replay across 8-bit
ID reuse is outside that error model, so CRC8/IDs do not provide authentication.
There are no deferred minor review findings.

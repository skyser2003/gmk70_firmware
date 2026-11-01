# Custom behavior consistency — offline validation

The four confirmed repository-specific gaps are covered by native-path
regressions: shared VIA/Vial radio recovery, ZMK native map-replacement pairing
cancellation, deferred RGB persistence, and independent held flip modifiers.
The [fixed checklist](../../CUSTOM-BEHAVIOR-CHECKLIST.md) records the acceptance
scope, evidence and limits. Fundamental framework feature sets and never-added
stock functions are excluded.

Work includes the existing uncommitted parity changes on main, based on
`7a8d7da8fa68c4c1b1db9c50a69948c999536363`. No commit, push, branch creation,
device query, flash, reset or private-map read was performed.

## Verification

`make test`, `make test-zmk` and `make check-tools` passed after the final code
and artifacts. All edited C/headers pass clang-format; changed
shell scripts pass shfmt and Bash syntax checks. Dedicated native regressions
use ASan/UBSan and strict warnings. Python checks use the pinned offline uv
projects with Ruff and strict mypy.
The default tool suite skips its optional built-resident check; that one test
was then enabled explicitly and passed against the compiled production resident.

All 19 affected profiles build: five QMK, seven Vial, and seven ZMK. The retired
ZMK handoff aliases are not counted again. All seven image reports admit exactly
`[0x20000040, 0x20006fc0)`, excluding both retained regions. Final Studio images:

| Profile | Application bytes | RAM used / capacity | Unallocated |
| --- | ---: | ---: | ---: |
| wired-studio-updater | 85,456 | 28,200 / 28,544 | 344 |
| wired-studio-update-test | 85,472 | 28,216 / 28,544 | 328 |
| wired-studio-radio-qualification | 85,968 | 28,224 / 28,544 | 320 |

Canonical application SHA256:
`d19f2d0672e0fb1b9070ef0e1811e39ffacc185472156c74ef139c6b4d93c16d`.
The RGB adapter adds 32 linked RAM bytes and modifier ownership adds 24, relative
to the 28,144-byte pre-batch allocation. No stack was reduced or thread added.

All fourteen packaged resident files match their pre-batch SHA256:
`94427de2fad50bb91337ded4341df02cab9070c550042a9832b3e12366791bf8`.
The padded resident SHA256 remains
`be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`.
Dependency pins, original VIA definition, board/default keymaps, custom keycode
values, native settings formats/partitions and permanent ZMK no-watchdog policy
are unchanged. Twelve untouched pre-existing parity source/test blobs were also
compared to the initial dirty-tree snapshot. The real Git index remains unchanged.

Each implementation passed a task review. Review caught a missing RGB target
include path and an in-flight clear/press race; both received focused fixes and
clean scoped re-reviews. The final whole-batch review found no Critical or
Important defect. Its one Minor NKRO checklist wording correction passed scoped
re-review; no findings remain. Native cancellation tests count their expected
debug events and assert zero native error logs.

Existing ZMK configuration/link warnings remain: optional Dtc unavailable,
devicetree flash unit-address mismatch, deprecated KSCAN, empty driver libraries
and RWX ELF segments. No new owned-C warning was reported. Build/test commands
loaded `scripts/project.sh` and used pinned dependencies without fetching them.

## Decisions and consequences

| Decision | Reason and consequence |
| --- | --- |
| Continue in the original dirty main checkout; preserve earlier changes | It contains the maintained implementation. Baseline snapshots and blob comparisons protect prior work; there is no integration commit or remote change. |
| Keep the four fix families and fixed checklist | The physical invalid-mode edge belongs to host/pairing admission. This is a bounded review of repository additions, not a claim about all upstream features. |
| Discard held report state on real radio loss/host change | It prevents stale keys returning. A still-held key must be released and pressed again after recovery; same-channel wake preserves held input. |
| Latch parser recovery and consume it in normal task context | Native report APIs stay outside parser ownership; output remains inhibited until cleanup runs. |
| Cache the exact opaque native RGB bytes | No alternate schema or generic buffer expansion. A future pin changing the ten-byte native ABI must fail the maintained round-trip test and receive review. |
| Preserve native implicit state only during custom mapped dispatch | Custom modifiers compose independently; unaffected native events keep their original semantics. |
| Use recursive thread-context serialization and owned nested scopes | No spinlock spans native dispatch. Future ISR keycode emitters would need to defer to thread context. |
| Require the NKRO report adapter, without requiring radio for flip | Getter composition is explicit, and USB-only Studio keeps a conditional clear owner. Flip cannot be selected without its report adapter. |
| Preserve consumer release context on keyboard-only clear | Modifier contributions disappear immediately; the consumer target still receives its own clear or balancing physical release. |
| Preserve fatal storage-fault policy | Pending RGB bytes remain in RAM, but latched faults are not auto-remounted or reformatted. Power loss before a healthy save can lose unsaved state. |
| Use temporary-index snapshots for reviews and leave changes local | The user's index/branch remain intact. Private local evidence is retained separately; publishing or installing is a separate action. |

No hardware qualification is claimed. RF delivery, electrical LED behavior,
silicon interrupt timing and runtime stack watermarks require device testing.
The LEFT automatic-return investigation remains deferred. Private device backups
stay ignored and local. See [result.json](result.json) for the build inventory.

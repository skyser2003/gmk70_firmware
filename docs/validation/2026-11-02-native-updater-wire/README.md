# Native updater wire validation — 2026-11-02

Both joined halves now run the same canonical application: **89,308 bytes,
CRC32 599821369**, SHA256
`50e1b919bccd6a9680f410f43f9125cc67541ba9061034722146d19eb745b3d5`.
Both resident CRCs remain **1770733249**. No resident write, manual USB cycle,
right-only connection or pad recovery was performed.

The right's final firmware transfer was sent by the already installed native-only
LEFT. That sender's validated ELF links `gmk_native_update_exchange`,
`gmk_native_update_serve`, upstream `zmk_split_wired_get_item` and CRC32, and excludes
`gmk_protocol_update` and `gmk_native_update_bridge_exchange`. Thus the completed
right firmware transfer exercised native ZMK framing, not legacy updater RPC18.
Matrix/RGB and behavior RPC17 remain board protocols. See the
[implementation and migration design](../../ZMK-NATIVE-UPDATER-WIRE.md).

## Before device writes

`make test`, `make test-zmk` and `make check-tools` passed. All eight affected ARM
variants built, including the separately named bridge. The built-resident pin
test was explicitly enabled and passed. Native sanitizer tests execute the actual
pinned ZMK parser, Zephyr ring buffer and CRC32 implementation. Coverage includes
all single-bit frame corruptions, truncations, semantic header errors, no replay,
legacy shared scratch, asynchronous INFO/STATUS handling, malformed legacy proofs,
native UART dispatch and role/mode admission. Independent review findings were
fixed and rechecked before flashing.

| Canonical Studio allocation | Before migration | After migration |
| --- | ---: | ---: |
| Application bytes | 88,424 | 89,308 |
| Allocated RAM, including stacks | 28,322 | 28,322 |
| Admitted RAM | 28,544 | 28,544 |
| Unallocated RAM | 222 | 222 |
| Reserved thread/interrupt stacks | 14,336 | 14,336 |
| Four mutable 74-position keymaps | 4,736 | 4,736 |

The bridge allocates the same RAM. No heap/thread was added or stack reservation
reduced. Individual compiler frames include native receive152, native send80,
upstream parser40, native responder80 and bridge120 bytes. These are not live peak
stack measurements.

## Joined-LEFT migration

| Step | Target and image | Before / after health seconds | INSTALL requests |
| --- | --- | ---: | ---: |
| 1 | LEFT bridge: 89,632 / 501955662 | 70.234 / 70.172 | 1 |
| 2 | RIGHT bridge: 89,632 / 501955662 | 70.187 / 70.187 | 1 |
| 3 | LEFT canonical: 89,308 / 599821369 | 70.187 / 70.219 | 1 |
| 4 | RIGHT canonical: 89,308 / 599821369 | 70.187 / 70.171 | 1 |

The listed successful windows verified exact both-half images/residents, physical
roles, idle updater states, advancing uptime and zero watchdog feeds. Each listed
window has zero transport retries, relay retries and reconnects. Read-only Studio
verification after **each** step preserved all four layer names/order, **296 saved
bindings**, geometry and runtime binding metadata. No layout write/import was
performed.

## Exceptions and limits

The first post-step2 strict observer rejected its unsaved report with
`Incomplete or retried both-half qualification`. That rejected window is not
counted above. A fresh 70.187-second diagnostic had zero transport/relay retries,
then the listed strict qualification passed. No flash/reset was repeated.

The final step4 INSTALL was dispatched once and reached its durable request
marker, but its acknowledgement failed the existing host verifier with
`INSTALL acknowledgement differs; never replay`. **INSTALL was not resent.**
Subsequent read-only INFO independently reported the expected canonical image on
both halves. A fresh 70.203-second diagnostic and the listed 70.171-second strict
qualification passed with exact identities and zero retries/reconnects/feeds;
Studio then independently verified the original saved map.

The original mismatched Reply fields were not recorded, so its precise cause is
unresolved. Code review identified a possible path: a failed relay exchange yields
a cached UNAVAILABLE response that `request_install` treats as a mismatch.
Flash programming masks interrupts, so native frame pacing/FIFO handling is one
possible source of a transient exchange failure, but no FIFO overrun or timing
cause was measured. Do not present that hypothesis as a confirmed cause, or the
whole session as having zero errors. The verified installed images establish
successful installation independently of the missing valid acknowledgement.

Physical typing/knob motion and visual RGB were not independently actuated.
Hardware stack watermarks and a live QMK/Vial round trip were not tested; legacy
bridge behavior was validated against source and dedicated regression fixtures.

## Provenance

The actual reviewed predeployment working tree is
`971547f337e67dbdda76a9cf4c69330b1280206a`, based on commit
`fccfead46bc22627c470fd4b3d81f3cc1900d26f`. Archive SHA256:
`2ebd7bd13847a5d69401f3c41037f89bd4bfef4b94e291f17bc0c9b745c2b94b`;
patch SHA256: `a893339436c860468b33ca1191730bab1abb582bf1bc07602ba46e485b4b8070`.
The frozen helper's source_commit names that base; source-tree/archive records
identify the uncommitted changes. Only validation documentation changed afterward.
At deployment, no unsigned commit or push was created. The implementation and
sanitized validation are subsequently published together in a signed commit on
`codex/zmk-native-integrations`; the frozen identities above continue to identify
the source actually used for device qualification.

Before source publication, `make test test-zmk check-tools` passed again. Fresh
canonical and bridge ARM builds reproduced the validated application binaries
byte for byte. All ten package tests passed with
`GMK70_TEST_BUILT_ZMK_RESIDENT=1`, including the actual built-resident checksum
check. Publication required no additional device operation.

Frozen packages/runtime, per-step INSTALL-once markers, full health evidence,
build/test logs, source archive/patch and private Studio backups remain under
`.work/zmk-native-updater/`. Personal bindings are excluded from this record.

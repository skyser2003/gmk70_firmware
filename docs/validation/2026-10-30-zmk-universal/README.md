# Universal ZMK installation validation — 2026-10-30

See [architecture, package and controls](../../ZMK-UNIVERSAL.md) and the aggregate
[result](result.json). Signed firmware source is
`bf0ccad85030f886e39aa13c0663e8c022aab654`.

The same normal `wired-studio-updater` application is installed on both halves:
79,624 bytes, CRC32 `893851802`. The installed package's application SHA256 is
`1e3cfa64060fbbeb7832dd8f5545d45bdba1cd4c6c212cfb2d38bf633739a1d9`.
Both runtime size/CRC identities match the reviewed package. Both resident CRCs
remain `1770733249`. The new profile is explicitly `zmk-studio-universal`;
older `zmk-studio` packages retain their LEFT-only gate.

Two frozen cases each sent INSTALL once, received the correct native
acknowledgment and independently verified automatic return. LEFT was installed
first while RIGHT remained 43,752/2368050678. Its full saved map and both-half
health were checked before qualifying and installing the identical application
on RIGHT through LEFT USB. No physical reconnect, right-only USB connection,
recovery pad, resident write or INSTALL replay occurred.

Before and after each installation, both applications/residents and advancing
uptime passed independent 70-second qualification with zero transport/relay
retries, reconnects or watchdog feeds. The selected sealed session and received
count were rechecked before the durable INSTALL marker; its peer stayed idle
on its exact qualified image. See the aggregate for health-window durations.

Fresh clean Studio settings were backed up before the first write. New serial
connections after each installation compared all 288 bindings, four layer
identities/names/order and capacity against that original, validated physical
geometry and runtime behavior metadata, and confirmed no unsaved changes.
No saved binding or factory default was changed. Normal Studio is connected
and displays all four saved layers. The final additional 70-second both-half
health check also passed; physical observations are recorded in the aggregate.
The user confirmed that all keys work, RGB works on both halves and the saved
lighting-control action works. The original Fn+X test suggestion did not match
the user's remapped lighting binding; the saved mapping was preserved.

Two early desktop reconnect attempts failed. Independent serial RPC and a
temporary inspected app connection both verified the device and clean saved
state. Explicitly closing the app transport and allowing initialization time
before the final normal launch restored the desktop connection, without another
firmware write. The precise cause of those startup failures was not isolated.

## Source and automated validation

RIGHT's backend skips mounting its settings-bank address and leaves readiness
false, so saves return immediately before any storage claim or worker queue.
Native tests prove this with blank and inaccessible bank memory; secured Studio
editing remains locked. Pinned upstream settings commit still assigns RAM-only
behavior IDs, and ordinary peripheral services continue. LEFT's existing
startup/write/readback/fault-lock and runtime USB dispatch tests remain green.

A fresh independent review found one important packaging-proof gap: the first
ELF checker accepted conditional branches with the wrong direction, register
or target. Six new negative regressions failed before the fix. The strengthened
checker admits the pinned compiler's decoded CBZ-r0 pattern, requires LEFT's
straight-line path to the single mount, prevents RIGHT re-entry and rejects
unsupported control flow. The current canonical ELF and all affected variants
pass it. No Critical or Minor findings were reported; there was one fix pass.

Final `make test test-zmk check-tools` passes 88 repository, 22 migration,
16 battery diagnostics, 187 updater and 27 ZMK Python tests, plus native
sanitizer suites. One optional built-resident integration test is skipped.
Ruff, formatting and strict mypy pass; edited C is formatted. The central,
peripheral, Studio update-test and radio-qualification ARM builds pass, with
only the normal universal application installed. Upstream dependencies remain
unchanged. Existing KSCAN deprecation and RWX linker warnings remain.

The universal image uses 27,576/27,648 bytes of linked static RAM. It retains
the prior Studio stack configuration; RIGHT now carries that same full image
and allocation rather than the smaller peripheral build. Actual hardware stack
watermarks remain unmeasured. No deliberate cold power cycle or RIGHT-only USB
central test was made. Wireless pairing, dongle input and battery life were not
requalified by this update. No rollback guarantee is inferred from power cycling.

Private full mappings, UI snapshots, frozen cases, raw logs and verification
scripts are archived locally with a checked SHA256 inventory and excluded from
Git. Archived operators are provenance, not resume commands. Public evidence
contains only allowlisted aggregates, without personal keymaps or host/bond data.

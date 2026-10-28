# GMK70 USB ZMK Studio

The user requested Studio connection and editable, persistent keymaps on the
already installed permanent wired ZMK port. Implement in this repository and
the existing task checkout, using the pinned upstream Studio implementation.
Prior authorization to continue autonomously remains applicable. Hardware work
is separate from builds/tests and follows code review and offline verification.

## Outcome and boundaries

- Add a separately labelled experimental `wired-studio-updater` build. Studio
  uses standard USB CDC-ACM; its physical layout covers the existing 72 positions
  and its initial keymap has the existing base/Fn layers.
- Keep the existing unlocked development policy, including Studio editing, for
  this explicitly labelled development image. Ordinary QMK/Vial builds retain
  their current policy. No wireless, RGB or encoder rotation is added here.
- Preserve resident bytes/CRC `1770733249`, retained SRAM exclusions, common
  updater ABI and all external Vial settings. Keep upstream checkouts unmodified.
- Install and validate the central LEFT application only. The existing RIGHT
  wired scanner/updater can supply all right-hand key positions without Studio.
- A successful setting save survives normal restart and compatible Studio
  firmware updates. A power cut during a multi-key Save may retain a durable
  prefix: pinned Studio saves individual settings, not one whole-map transaction.

## Approaches and chosen design

Standard CDC plus today's separate keyboard/updater HID interfaces would need
four IN endpoints; WB32 has three. A shared interrupt HID stream would require
new packet arbitration with upstream keyboard transmission. A reduced-endpoint
CDC implementation would diverge from the pinned class and need independent OS
driver qualification. Choose standard CDC and a vendor **Feature** collection
on keyboard HID0: control transfers do not consume another interrupt endpoint.

Use report ID **4**, 32 payload bytes, vendor page `FF60`, usage **62**. Existing
usage 61/unnumbered interrupt framing remains unchanged for every non-Studio
image. Studio has one keyboard HID IN plus CDC notification/bulk IN and bulk
OUT: three IN and one OUT beyond EP0. A Studio-only registration wrapper appends
the vendor descriptor to the actual upstream descriptor and forwards its existing
callbacks. It must register HID0 once, preserve keyboard reports/semaphore, and
never execute flash or relay work in a USB callback. GET_FEATURE returns a stable
snapshot; SET_FEATURE invalidates stale transport results and enqueues one bounded
request for the existing live worker.

Host discovery selects usage 61 or 62 explicitly. Shared send/read helpers in
`UsbLink` keep updater, trial and health correlation intact across reconnection
from the old normal image to Studio. No mutation probe, product-name guess or
driver replacement is needed. A synchronous OS feature call still has an OS-owned
timeout; do not claim the host's loop deadline can cancel such a call.

## Persistent settings

WB32 vendor ECC restrictions permit an erased page to be programmed once. Ordinary
NVS partial append writes cannot safely be emulated by destructive page rewrite.
Use Zephyr `SETTINGS_CUSTOM` with immutable packed snapshots and fresh whole-page
journal records, in two independent 8 KiB banks:

| Address | Purpose |
| --- | --- |
| `08000000..08001fff` | Existing resident, unchanged |
| `08002000..0801bfff` | Studio application, at most `0x1a000` bytes |
| `0801c000..0801dfff` | Settings bank A |
| `0801e000..0801ffff` | Settings bank B |
| `08020000..0803dfff` | Existing updater staging |
| `0803e000..0803feff` | Existing reservation, untouched |
| `0803ff00..0803ffff` | Existing commit page, unchanged |

Keep RAM admitted only at `20000040..20006fbf`. Do not allocate an 8 KiB RAM
mirror. Bound names, values and live-entry index; retain native behavior-ID,
keymap, layer-name/order and physical-layout settings. Reject unsupported sizes
and full storage before destroying durable data. Skip identical saves.

Each bank has a full-page commit written last, with version, generation, packed
snapshot extent and CRC. Data/journal pages include generation, sequence, lengths
and CRC. Reconstruct the latest valid bank, replay only its valid ordered journal
prefix, and never expose torn records to settings handlers. Compaction streams
the live map into the other bank, verifies it, commits it last, then changes the
active bank. An unsuccessful write requires rescan before another save. Never
silently format nonblank corrupt or unsupported storage. CRC is a recovery check,
not a hardware atomicity guarantee.

The updater worker owns flash operations for both updating and settings. A settings
caller queues one bounded request and waits for its durable result; the worker
does not call settings APIs while the caller holds the settings lock. Reject
settings writes during firmware receiving/sealed/committed states and reject
BEGIN while a settings operation is admitted. Preserve watchdog feeding only
after completed bounded work; do not feed through a stuck FMC operation.
Trusted initial behavior-ID persistence must work before USB configuration.
Surface storage initialization failure and fail Studio mutations closed even
though pinned main/settings code ignores some load errors.

Keep `gmk_flash_page()` stage/commit guards. Share its exact vendor page engine
with a separately guarded Studio-settings adapter; do not weaken resident/option
or stage-execution restrictions. Enforce Studio's `0x1a000` image limit in linker,
actual ELF/package checks, host profile `zmk-studio`, and local receiver admission
before BEGIN clears metadata. Profile `zmk-studio` installs only on LEFT. Old or
non-Studio firmware can overwrite the app-tail storage; persistence is guaranteed
only for compatible Studio update paths, not arbitrary platform changes.

## Layout, dependencies and verification

Generate Studio physical metadata under the build directory from immutable VIA
KLE coordinates, ordered and validated against authoritative `keyboard.json`
matrix positions. This avoids propagating known malformed QMK display coordinates
and does not change either source. Add exact Studio-messages and nanopb pins from
the already pinned manifests, with explicitly locked protobuf build tools. Only
`make setup-zmk` downloads sources/tools; builds remain offline.

Test real descriptor/callback behavior, both host framings and correlation,
malformed packets, settings page power-cut recovery, compaction, capacity and
region guards. Build/check real Studio and affected existing ZMK images; inspect
USB endpoints, linked wrapper, source pins, flash/RAM and unchanged residents.
Perform one fresh code review before hardware writes. Then capture actual LEFT
Studio USB/serial/feature identities and RPC layout/keymap, test save/reload and
compatible self-update retention, and verify both native runtimes/updaters.
Check for actual USB return before any joined-left reconnect fallback. Never
repeat INSTALL merely because restart verification timed out. Do not flash RIGHT
for this feature. Record actual results and residual hardware limits honestly.

References: [Studio](https://zmk.dev/docs/features/studio),
[physical layouts](https://zmk.dev/docs/hardware-integration/physical-layouts),
[RPC transport](https://zmk.dev/docs/development/studio-rpc-protocol), and pinned
vendor page-program source documented in `firmware/zmk/PROVENANCE.md`.

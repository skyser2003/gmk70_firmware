# Permanent wired ZMK with the existing updater

The user requested permanent ZMK that remains updatable through the existing
left USB updater. Earlier autonomous execution authorization and the preference
to avoid right-only physical recovery continue to apply. Design choices below
are implementation decisions, not additional user preferences.

## Goal and constraints

Deliver genuine pinned ZMK running from the normal application slot on both
halves, a working wired matrix path, and software updates/rollback through left
USB. Preserve both installed residents byte-for-byte, native saved Vial EEPROM,
the stock VIA definition, dependency pins and unmodified upstream checkouts.
Builds/tests never open hardware or download sources. Maintained integration
stays in this repository under `firmware/zmk/`; generated data stays in `.work/`.

The existing flash contract remains: resident at `0x08000000..0x08001FFF`,
application at `0x08002000` with maximum `0x1E000` bytes, staging at `0x08020000`
with the same maximum, and the 256-byte commit page at `0x0803FF00`. SRAM remains
below `0x20007000`, with QMK boot magic preserved. No application adapter may
write residents or running application pages. Retain ABI1 updater packets and
image/commit validation. Invalid, stale and interrupted updates must fail safely.

This milestone is wired USB operation. Radio, RGB, encoder rotation, low power,
Studio and persistent ZMK settings are future work; ordinary keys on both halves
and updater availability are required. Physical key input cannot be claimed from
an initialized matrix alone.

## Approaches considered

1. Replace the resident with a ZMK-specific bootloader. This expands physical
   recovery exposure and conflicts with resident preservation; reject it.
2. Permanently retain Vial as a supervisor and run ZMK from staging. Updating
   that staging image while it executes is unsafe and this would not make ZMK
   the installed application; reject it for the requested outcome.
3. Add native Zephyr adapters to the existing updater contract and run ZMK in
   the normal application slot. Choose this: it preserves residents and keeps a
   path between QMK/Vial and ZMK. It requires native flash, UART and USB integration.

The user already opted out of design questions in this ongoing task. Proceed
with approach3 and review/test it before hardware writes.

## Firmware architecture

Build one universal `gmk70_wired` image for both halves, sensing handedness B9
using the maintained board's electrical convention. ZMK receives a 10-by-9
logical matrix: left local rows0..4 (eight populated columns), right rows5..9
(nine columns). Its compile-time keymap follows the existing physical key
assignments, with a useful function/navigation layer. No EEPROM initialization
or writes occur. Preserve the existing experimental trial targets unchanged.

Use the existing portable `image.c` and `protocol.c` directly from
`firmware/qmk-common/.../update/`; do not duplicate or modify them. Native WB32
flash programming follows the pinned vendor's RAM operation/pre-operation
sequence, limits writes to staging/commit, preserves interrupt/clock state and
verifies readback. A permanent watchdog service handles a potentially inherited
running IWDG on entry; it must continue while the application works and permit
bounded update/restart operations. Staging qualification never refreshes IWDG.

Native UART1 on A9/A10 uses the existing 115200 8N1 full-duplex wiring. Implement
the pinned Vial updater transaction profile for migration compatibility and use
it between ZMK halves too. Frame IDs, lengths, XOR handshake and RPC CRC must
match the actual installed Vial profile; malformed/delayed frames must not cause
buffer overflows or mutation. The left polls right matrix rows and releases
remote keys after disconnection. UART transactions are serialized against updater
RPC; no flash operation runs in an IRQ or holds up a serial handshake.

Expose keyboard HID and a separate 32-byte raw HID updater interface using this
GMK70's existing `342D:E491`, manufacturer `HS`, usage page `FF60`, usage `61`.
Keep the host protocol and installed resident identity checks. New package
profile `zmk` denotes a genuine application-slot ZMK build; the host must still
reject wrong addresses, vectors, resident hashes or payloads. Both halves use the
same binary, permitting normal right-first updates after migration.

An optional permanent-development-only `CONFIG_GMK70_ZMK_PEER_TRIAL_RELAY`
forwards validated namespace0x71/ABI2 packets solely to target1, using the common
trial packet validator directly. It never executes a local trial or changes the
resident. Separate namespace ACK caches prevent STATUS from exposing replies
from another protocol. The same USB role/mode/watchdog authorization applies.
This permits right staging qualification while native ZMK runs on the left,
before replacing the right application. It is disabled in staging qualification.

Continuous runtime evidence uses a separate read-only namespace `0x72`, ABI1,
operation `0x20`, with nonzero nonce at byte8 and no other request payload.
The response echoes the header/nonce, reports normal role bits at byte7 and
magic `0x4B4D5A47` at byte12, then uptime_ms, feed-call count, health flags and
matrix-event count as four little-endian words at byte16. Flags are watchdog
ready bit0, wired mode bit1, peer connected bit2, staging qualification bit3.
This leaves normal INFO and updater ABI1 unchanged and permits either ZMK half
to supply runtime health through the same left USB relay. It is not supported
by the older Vial application and must not be mistaken for its trial health ABI.

The development build is explicitly labeled unlocked. Only a left unit with
configured USB and the wired-mode switch selected may accept host mutations;
right requests inherit that master's authorization. Normal Vial lock behavior
and all published Vial release assets remain unchanged.

## Qualification and installation

Freeze the current left/right Vial application packages, resident hashes and a
fresh native configuration backup. Establish fresh read-only identities first.
Review native MMIO tests, wire compatibility tests, host package tests and actual
linked ELFs before installing any application.

Use a separate staging qualification build with bounded watchdog return and
no ability to overwrite its running staging range. Verify USB updater discovery
and right INFO/matrix transport while the right still runs Vial. Any permitted
flash probe is restricted to erasing the already-erased commit page; it must not
stage over running code or make an install durable.

Install the left application first through the working Vial updater; retain
right Vial until the ZMK left exposes verified INFO for both halves and proves
its staging flash path. Verify a ZMK-left software update path before
installing right ZMK. Stop dependent hardware writes on failed identity/transport
checks and diagnose; do not blindly retry or ask for right-only physical recovery.

Freeze two reviewed ZMK builds with a recorded USB product-string difference
for update proofs: normal `GMK70 ZMK Wired` and `GMK70 ZMK Wired Update Test`.
The pinned legacy USB stack has no device-version Kconfig; this string difference
gives different image content without changing keyboard/updater behavior.
Qualify a left self-update while
the right still runs Vial, then launch a right staging qualification using the
peer trial relay and observe right ZMK INFO/health before replacing its app.
Do not count an identical-image resident page skip as a different-image update.

An IWDG started by permanent ZMK may survive a warm firmware replacement. Frozen
Vial services it only when its retained trial recovery gates qualify. General
automatic Vial rollback after arbitrary cold boots is therefore not established;
it may require a joined-left power cycle. Keep its exact recovery images and
native backup, but do not introduce fabricated trial records or modify residents
to claim a seamless handover. This milestone qualifies ZMK-to-ZMK updates.

After both run ZMK, verify a right application update through left USB, a left
self-update, stable boot/uptime, unchanged residents, and matrix/HID behavior.
Record exactly which physical input was observed; ask for a key press only if
host-side evidence cannot establish it. Keep recovery images and private native
backups available; never publish keymap/macro payloads in validation reports.

## Evidence required for completion

- Genuine application-slot ZMK ELF/image and reproducible pinned build.
- Both halves identify the expected installed images and unchanged residents.
- Left USB keyboard and updater interfaces remain available beyond recovery
  watchdog deadlines, with advancing uptime and no unobserved reboot assumption.
- Right updates and left self-updates complete over left USB; compatible Vial
  rollback packages remain accepted.
- Native saved configuration stays untouched; ZMK's compile-time layout and
  deferred features are stated plainly in the final report.

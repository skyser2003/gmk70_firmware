# Native ZMK framing for the updater relay

The canonical image is installed on both halves, including a real right-half
firmware transfer sent by native-only LEFT. See the
[hardware validation and acknowledgement exception](validation/2026-11-02-native-updater-wire/README.md).

The application updater now has a native ZMK frame carrier between the halves.
The resident installer, USB packet ABI, staging/validation/install state machine,
flash ownership, physical-role gates and permanent no-watchdog policy are unchanged.
One identical canonical application still supports both physical halves.

This uses the unchanged pinned ZMK `zmk_split_wired_get_item()` parser and
Zephyr ring buffer/CRC32 implementations. WB32 UART1 remains the board backend;
the upstream role-specific UART driver stacks are not enabled. Matrix snapshots,
RGB streaming and behavior RPC17 retain their existing board protocols.

## Wire format

The native `ZmKw` prefix, payload length and CRC32 framing are reused. Source 0
identifies the sole peripheral. Board extension command 0x80 carries a 32-byte
updater request; board extension event 0x80 carries its 32-byte response. Native
command types occupy one byte; event types occupy four on the pinned WB32 ABI.
Requests are 43 wire bytes and responses 46. The original updater packet stays
intact inside each envelope, including command, session, offset and up to 16
firmware bytes. No resident understands or needs this carrier.

Both directions validate frame CRC32, exact length, source and extension type
before dispatch. Requests use the existing queue/cache callback; flash work
remains on the dedicated service worker. A failed exchange returns unavailable;
it never resends or changes carrier after uncertain delivery. Existing host
logic retains its operation correlation and INSTALL-once policy.

The largest receive buffer is stack-local, 46 bytes. No heap, thread or additional
stack reservation was introduced. The existing 20 ms transaction budget and
128-byte UART queue bound I/O. Static compiler frame records are reviewed for
the serial and updater call paths; hardware stack watermarks remain unmeasured.

## Migration bridge

Canonical images reject updater RPC18 setup and must not link its legacy relay
function. The separate `wired-studio-native-bridge` build explicitly enables
both carriers. Its native-capability query uses existing RPC17 with a harmless
`ZMKU` versioned request. Only the exact checksummed native advertisement permits
sending native bytes to a peer.

Older QMK/Vial handlers may leave stale shared RPC response bytes when rejecting
that query. The bridge therefore proves legacy service availability with a
read-only RPC18 STATUS probe and validates its updater header, known result/state,
right role and ABI. STATUS can legitimately return a cached INFO/DATA/BUSY/error
reply. The actual caller retains normal asynchronous operation polling. A failed
discovery invalidates the cached route; no mutation is sent until discovery
succeeds. INFO/BEGIN refresh the route after application transitions.

The transition order is LEFT bridge, RIGHT bridge, LEFT canonical, RIGHT canonical.
The last RIGHT transfer is therefore forced through a native-only LEFT sender.
Each step requires exact both-half identities/residents, a clean saved Studio
backup, independent health qualification and saved-map comparison. No right-only
USB connection or recovery pads are part of this workflow. The bridge remains
available for future migrations to older QMK/Vial applications: install it on
LEFT before changing RIGHT to a legacy-only application.

## Qualification

Tests execute the actual pinned native frame parser, ring buffer and CRC32. They
cover all single-bit corruptions in both frames, truncated frames, semantic
header errors with recomputed CRC, no retransmission, persistent legacy scratch,
asynchronous INFO/STATUS behavior, malformed legacy service proofs, role/mode
admission and actual WB32 UART dispatch. Linked-image checks require native
transport symbols and allow legacy relay symbols only in the explicit bridge.

Canonical and bridge Studio builds both allocate 28,322 / 28,544 RAM
bytes, leaving 222 unallocated, with unchanged 14,336-byte stack reservations and
4,736-byte mutable keymap storage. The validation record contains exact source,
package and installed image identities, successful health windows and remaining
limits. The final INSTALL acknowledgement did not validate; exact installed
images and subsequent independent health/Studio checks proved success without
replaying INSTALL. Its precise acknowledgement-loss cause remains unresolved.

# Native ZMK integration and compatibility transport

The updater relay was subsequently migrated to
[native ZMK framing](ZMK-NATIVE-UPDATER-WIRE.md), while matrix/RGB and behavior
RPC17 retain board transport. The remainder of this document describes the
preceding native-core/encoder integration checkpoint.

The implementation is installed on both halves. See the
[hardware validation and its limits](validation/2026-11-02-native-zmk/README.md),
including the initially rejected post-RIGHT health window and subsequent clean
checks.

The GMK70 port now uses the pinned upstream EC11 driver and generic ZMK split
central/peripheral cores. Both role cores are linked into the same application;
PB9 still selects the physical role at runtime. Dependencies remain unmodified.

This is **not a switch to upstream's wired UART transport**. GMK70 retains its
board transport because it carries updater RPC, pixel frames and legacy matrix
snapshots. Those snapshots repair lost transitions and release held remote keys
after the existing 100 ms timeout. This also permits a staged update while the
other half still runs an older compatible application.

## Knob

The native EC11 device is deferred until the board has identified LEFT. RIGHT
never initializes its encoder pins. The native driver performs GPIO sampling
and quadrature decoding; the board IRQ adapter keeps detent aggregation, a
bounded FIFO and the two ordinary Studio key positions (72/73). Saved direction
bindings and their press/release pairing remain unchanged. No sensor thread or
additional stack is reserved. The pinned driver's tick compatibility interface
is used with its default resolution of one edge; the board's four-edge detent
resolution remains authoritative.

Native invalid-transition handling is now used: an invalid Gray transition
contributes no tick. The former custom decoder cleared its partial detent on a
two-bit transition. The native decoder is intentionally not overridden to
reintroduce that policy.

## Split events and behaviors

The existing native physical-layout workqueue still delivers matrix events. A
board adapter identifies positions belonging to RIGHT using the selected native
matrix transform, then calls the upstream central event handler. That handler
raises one event with peripheral source 0. LEFT keys and knob positions retain
the native local source. RIGHT local scanning behavior is unchanged; it is not
suppressed by this adapter.

Upstream selects one generic role in CMake. The integration additionally compiles
the unchanged peripheral core and gives its translation unit's `active_transport`
global a distinct symbol name. Each core selects the board transport only on its
matching physical half. This is a build adapter, not a maintained source copy.

Native event-source behavior commands use RPC 17 with a versioned 32-byte codec
and CRC8. Only INVOKE_BEHAVIOR is accepted; callbacks enqueue, and the existing
service worker performs UART/execution outside callbacks. The two-command queue
is bounded. Commands are not retried after uncertain delivery. This is not a
guaranteed-delivery behavior channel: full queues/disconnection may drop a command,
as native event transports can also reject queued work. Legacy USER_SYNC lengths
remain ignored with zero responses; updater RPC 18 is unchanged.

The board transport is not counted by upstream's BLE/UART-only peripheral-count
macro. Generic native GLOBAL behavior fan-out therefore remains absent. Current
RGB works through the single central 77-pixel renderer and board pixel stream,
including battery overlays. Native extension-power has no GMK70 device backend.
Adding another global peripheral behavior requires explicit transport integration;
this change does not claim general upstream UART feature parity.

The stock host radio, board GPIO/matrix, USB/flash integration, updater, settings
storage, function/media flip and accepted runtime NKRO toggle remain GMK70-specific
where required. There is still one identical universal application for both halves,
with target-specific updater manifests. Permanent firmware never enables or feeds
a hardware watchdog; the resident is preserved.

## Memory qualification before device writes

Canonical Studio comparison against main `fccfead46bc22627c470fd4b3d81f3cc1900d26f`:

| Linker measurement | Before | Native integration |
| --- | ---: | ---: |
| Admitted RAM | 28,544 B | 28,544 B |
| Allocated RAM, including reserved stacks | 28,200 B | 28,322 B |
| Unallocated RAM | 344 B | 222 B |
| Total reserved thread/interrupt stacks | 14,336 B | 14,336 B |
| Four mutable 74-position keymaps | 4,736 B | 4,736 B |
| General heap | 0 B | 0 B |

Reserved stacks remain Studio RPC 2,560; updater 2,048; interrupt 2,048; scanner
1,536; serial 1,536; system workqueue 1,536; main 1,024; USB 1,024; low-priority
workqueue 768; idle 256 bytes. No stack was reduced to make this fit.

Compiler stack-usage records give 48 bytes for the position adapter, 64 for the
native central event handler, 128 for the native peripheral command handler, and
144 for the command worker adapter. These are individual frames, not measured
peak call chains. The extra source-routing calls execute within the existing
1,536-byte system workqueue reservation; command execution uses the existing
2,048-byte updater worker reservation. Hardware stack watermarks remain unmeasured.
The 222-byte linker remainder is not the remaining space inside those stacks.

Offline checks include actual pinned EC11 and split-core execution under native
sanitizers, board GPIO/IRQ/fallback and legacy transport tests, strict Python
checks, repository tests and all seven affected ARM build variants. The resident
is independently checked against its pinned checksum. Hardware validation, when
performed, must separately establish installed identities, both-half uptime,
resident preservation and saved Studio keymap preservation.

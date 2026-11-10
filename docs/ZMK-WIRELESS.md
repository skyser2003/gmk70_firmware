# GMK70 Bluetooth and dongle support

The ZMK Studio central uses the existing GMK70 radio over UART3 for Bluetooth
and the proprietary 2.4 GHz receiver. It does not replace the radio firmware or
use Zephyr's Bluetooth stack. The existing RIGHT peripheral remains compatible
while the halves are joined; keep RIGHT's switch in its normal joined position.

Assign **GMK70 Bluetooth / Dongle** in Studio. Parameter 0 selects USB, 1–5 select
the corresponding Bluetooth slot, and 6 selects the dongle. A short press selects
the destination; holding a wireless selector for three seconds requests pairing.
Releasing or reassigning the held key cancels a pairing request that has not yet
been sent. There is no automatic bond erase or CLEAN command. The physical LEFT
host switch selects USB, the last saved Bluetooth slot, or the dongle after a 50 ms debounce.
Connection shortcuts can override that selection in any valid slider position;
the slider selects again only at startup or when moved. VIA and Vial share this
policy; see [changes from stock](../README.md#changes-from-stock).

Cancellation precedes native binding edits, Studio Discard/Reset and layer
move/add/remove/restore operations. It invalidates the physical deadline and
queued/pending pair even if native work blocks, fails or makes no change.
Commands already transmitted are outside this cancellation boundary. Dedicated
tests execute the pinned native keymap paths; see the
[custom behavior checklist](CUSTOM-BEHAVIOR-CHECKLIST.md).

Native USB remains available for Studio and recovery while typing is routed to
the radio. Firmware/settings writes still require LEFT USB, the physical wired
position, service readiness and the existing storage gates. Read-only radio
status remains available with USB connected in a wireless switch position.
Changing output clears keyboard and consumer reports before the new selection.

## Implementation and scope

The existing worker and battery UART owner serialize complete radio, battery and
acknowledgment frames. A bounded eight-report FIFO preserves fast taps; overflow,
disconnect and output changes reserve zero reports to clear remote held keys.
Control retries and partial writes have finite timeouts. Native tests cover
parser quarantine, ACK ownership, frame interleaving, overflow, cancellation,
physical mode changes, split/RGB continuity and actual linked HID clear calls.

Wireless reports support the existing six-key keyboard report and one consumer
usage at a time. Native ZMK Bluetooth profiles, Generic Desktop system reports, wireless
Studio and untethered split operation are outside this implementation. Native
mouse reports and the board sleep policy use the stock radio transport; see
[basic feature parity](ZMK-BASIC-PARITY.md). A bond stored by the stock radio is retained.
Neither battery life nor absent-receiver typing can be inferred from a successful
selection acknowledgment.

The canonical image excludes the F24 test command. A separately named temporary
`wired-studio-radio-qualification` image provides a fixed F24 press/release through
the normal ZMK HID path for narrowly filtered host verification. Its command is
not an arbitrary input injector. Both variants retain permanent watchdog removal,
resident identity, settings partitions, endpoint sizes and existing thread stacks.
USB RAM is reclaimed from unused IN-endpoint receive storage.

## Separate device operations

Build and test commands never control the keyboard. The separate updater tools
inspect current images/residents and verify health before and after installation.
INSTALL is dispatched once; an unknown outcome permits independent observation.
See the [updater workflow](ZMK-PERMANENT-WIRED.md).

`radio.py` provides explicit selection/pairing actions and cached status. Each
action is sent once. Personal exports, host identities, bonds and complete Studio
readbacks stay outside version control.

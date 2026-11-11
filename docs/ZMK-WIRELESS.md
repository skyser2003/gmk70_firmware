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

## Sleep and reconnect

A physical wake from retained STOP reselects the current Bluetooth/dongle
profile before reopening the scanner. A physical wake from USB shallow sleep
uses the same hook; a selected USB output needs no radio reselection. Quiet
sleep deferral and routine peer/UART maintenance do not request a reconnect.

SELECT's UART acknowledgment and the radio's CONNECTED notification are separate.
Missing connection status now permits three SELECT attempts, spaced five seconds
apart, with the existing finite transport-ACK retries and drain barriers. A new
DISCONNECTED transition also starts a bounded recovery window. PAIRING and
REJECTED stop automatic recovery; it never sends PAIR, CLEAN or identity changes.
An explicit selection of the same disconnected profile can restart recovery.

During the same-host wake window, the existing eight-snapshot FIFO retains native
keyboard/NKRO, consumer and mouse reports. CONNECTED does not clear those queued
wake events or native held modifiers. Reserved zero reports precede their delivery.
If the host remains unavailable after the final five-second wait, queued wake
events are discarded and native input is cleared so typing cannot replay much
later. A host change uses the existing clear/discard boundary immediately.
Retry deadlines do not count as user activity or an indefinite sleep veto, and
the paused UART/radio owners transmit nothing during full sleep.

The captured stock application (SHA256
`100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c`)
supports this non-destructive refresh: SELECT helper `0x10011A68` takes the
already-connected, same-profile branch at `0x10011B12` to emit status `0x32`
through `0x10011B80`. The original capture stays outside version control.
This is instruction evidence, not Bluetooth hardware confirmation of this fix.

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

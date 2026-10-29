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
host switch selects USB, the last Bluetooth slot selected during this boot, or
the dongle after a 50 ms debounce. The last-slot preference is currently RAM-only.

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
usage at a time. Native ZMK Bluetooth profiles, mouse/system reports, wireless
Studio, untethered split operation, radio power management and deep sleep are
outside this implementation. A bond stored by the stock radio is retained.
Neither battery life nor absent-receiver typing can be inferred from a successful
selection acknowledgment.

The canonical image excludes the F24 test command. A separately named temporary
`wired-studio-radio-qualification` image provides a fixed F24 press/release through
the normal ZMK HID path for narrowly filtered host verification. Its command is
not an arbitrary input injector. Both variants retain permanent watchdog removal,
resident identity, settings partitions, endpoint sizes and existing thread stacks.
USB RAM is reclaimed from unused IN-endpoint receive storage.

## Saved layout and remaining functions

The offline converter translates the eight wireless bindings in the user's saved
four-layer Vial export. Studio's `apply --only-behavior gmk_radio` restores those
positions from a fresh clean backup, preserving all other 280 bindings and all
layer names/order, then saves, reloads and compares the complete map.

Five of the original 13 unsupported bindings remain unassigned:

| Function | Bindings |
| --- | ---: |
| `custom(26)` / function–media flip | 1 |
| `custom(9)` / logo lighting effect | 1 |
| `custom(25)` / reserved board action | 1 |
| QMK EEPROM/settings reset (`0x7C03`) | 2 |

This is four function types occupying five positions. The conversion now supports
283 of 288 bindings. The factory keymap and private Vial export are unchanged.

## Separate device operations

Build and test commands never control the keyboard. `qualified_install.py`
creates a private frozen LEFT-only case offline, qualifies both current images
and residents for 70 seconds, and permits one INSTALL request after fresh
identity and uptime checks. It never retries INSTALL; an unknown outcome permits
only independent observation. A post-install observer checks the exact new LEFT,
unchanged RIGHT/residents and another 70 seconds of health.

`radio.py` provides explicit selection/pairing actions and cached status. Each
action is sent once. Personal exports, host identities, bonds, tickets and full
Studio readbacks stay outside version control. Installation and hardware results
are recorded separately after verification.

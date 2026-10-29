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
Studio readbacks stay outside version control. See the
[hardware validation](validation/2026-10-29-zmk-wireless/README.md).

## Installed result — 2026-10-29

LEFT now runs the normal 78,032-byte image, CRC32 `3592996973`, SHA256
`6eda0cddece0f035739bbf4ebe94ad9c91ea1d8f3170a92e1c24c14b070fd320`.
RIGHT remains 43,752 bytes/CRC32 `2368050678`; both residents remain `1770733249`.
The qualification and normal installations each sent INSTALL once and returned
automatically. No physical reconnect, RIGHT write or recovery pad was needed.
An overly strict host acknowledgment check rejected the first valid INSTALL
response; independent exact-image/resident observation verified that update
without replay. The helper and protocol-faithful regression are corrected.

Bluetooth slot 2 reused the existing Windows bond. Two separate, narrowly filtered
Windows Raw Input captures observed a complete F24 press/release pair from the
approved Bluetooth keyboard interface, including after switching away and back.
The final normal image also reconnects to that bond, reports no radio errors and
rejects the test command. USB output is restored; normal Studio remains connected.
The temporary test command is absent from the final linked image.

Bluetooth slots 1/2/3, dongle and USB selection were exercised on hardware.
The radio acknowledged dongle selection and reported connection status; no
receiver is enumerated on this host, so dongle typing is unverified. New pairing,
physical switch changes, consumer-key reception and battery-powered operation
were not tested on hardware. Native fixtures cover their implemented protocol
and selection behavior; no battery-life or untethered-split claim is made.

The eight wireless bindings were saved and independently re-read with correct
runtime metadata and geometry. Every other binding and all four layer names/order
are preserved. Final both-half health passed 70.140 seconds with zero retries,
reconnects or watchdog feeds. Battery telemetry remains fresh at 100%, cable/full
asserted. Static RAM is 27,488 of 27,648 bytes with unchanged thread stacks;
hardware stack watermarks remain unmeasured. The full native/Python suites,
Ruff, strict mypy, edited C formatting and affected ARM/image checks pass.

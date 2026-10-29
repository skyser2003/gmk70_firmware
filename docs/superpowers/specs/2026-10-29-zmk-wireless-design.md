# GMK70 vendor-radio Bluetooth and dongle transport

The user requests autonomous implementation and hardware testing while away,
with an active goal and a report of the original thirteen unsupported bindings
remaining afterward. Questions/design approval are explicitly waived. Continue
in the existing development branch and repository; use its pinned dependencies,
working resident, private settings backups and no-permanent-watchdog policy.

## Approach

Use the unchanged vendor radio over UART3. Its stock command/report protocol
already supports Bluetooth profiles and the proprietary dongle. Native Zephyr
BLE would require an established HCI transport that this radio does not expose;
replacing radio firmware could break the existing dongle and bonds. Neither is
needed for this task. Preserve all pairing data and never send CLEAN during
startup, selection or unattended tests.

Extend the existing bounded battery UART owner rather than introducing another
driver/thread. Local linker wrappers route ZMK keyboard/consumer reports to the
radio when selected; upstream USB endpoint identity and readiness remain real.
Wrap HID clear functions too: upstream endpoint changes internally bypass the
public report function. Leave imported framework sources unchanged.

## Behavior

Expose **GMK70 Bluetooth / Dongle** in Studio with parameter 0 USB, 1..5 Bluetooth
slots and 6 dongle. A press selects; holding a radio binding for 3000 ms requests
pairing without erasing existing bonds. Release, another selection, reassignment
or a physical-mode change cancels a pending hold. PAIR without CLEAN must be
hardware-qualified; report any module limitation explicitly.

At boot, physical switch 11 selects USB, 10 Bluetooth 1 and 01 dongle. Debounce
changes for 50 ms; invalid 00 must release input and cannot enable writes.
During a running session retain the most recently selected Bluetooth slot.
Explicit Studio radio bindings also allow USB-powered wireless typing in switch
position 11. A later physical-switch change takes precedence. USB remains
enumerated for Studio, control/status and recovery; USB keyboard output is
suppressed while a radio destination is selected. No persistent preference is
promised in this first adapter; radio bonds remain persistent in the module.

Retain the existing inter-half cable for split input and RGB in all valid LEFT
host modes. RIGHT can keep its existing firmware and USB-mode switch position.
Separate LEFT scan/peer availability from the strict physical USB/wired updater
and settings predicates. Do not broaden write admission merely because split
traffic works in a radio mode. Keep real USB readiness and fixed physical roles.

Translate the current HKRO6 keyboard state into the vendor eight-byte report.
The module supports one consumer usage; choose the first currently held usage
deterministically and restore the next held usage on release. Do not claim
native BLE, wireless Studio, NKRO, mouse/system parity or low-power/deep-sleep
support. Keep polling and the existing thread stacks; never start a watchdog.

Preserve key transitions in a bounded immutable FIFO. Queue saturation must
force zero reports before resynchronizing, never strand a key. Profile/output
switching must finish release barriers to the old destination before selecting
the new radio channel. Disconnection/rejection clears queued stale input;
reconnection starts with zero state. Mutate ZMK HID state only in its event/work
context, never from the updater worker, and never synchronously wait for that
work from the worker.

Serialize complete UART frames. ACK priority applies only between frames, with
one outstanding acknowledged control/query. Preserve bounded RX, partial TX,
finite retries, parser quarantine and fresh battery sensing. Status notifications
must not be accepted before current selection dispatch; the wire protocol has
no channel/transaction identifier, so document remaining ambiguity rather than
inventing correlation. No wireless RAW frame may enter updater dispatch.

Provide a separate explicit USB radio control/status tool for unattended tests.
Callbacks validate target, role, packet shape and nonce and defer state changes
to worker/event context. Control is separate from builds and read-only health;
it cannot erase bonds, write the resident or reset the keyboard. Hardware test
markers, if needed, must be restricted to a finite F24 press/release and followed
by releases, never arbitrary HID injection or capture of other user input.

## Memory and verification

Existing Studio uses 27,560/27,648 bytes static RAM. Move USB RX storage from
eight direction-specific endpoint states into four OUT-only packet buffers,
preserving all endpoint states, packet sizes and stack allocations. Budget the
bounded radio state against the actual ELF; do not repurpose settings IDs or
retained memory to hide an overflow.

Test frame bytes/checksums, ACK/control/report ordering, early/late ACK, partial
TX/stalls, retries/time wrap, disconnect/reconnect, release barriers/saturation,
mode debounce, hold cancellation, real multi-object linker wrappers and unchanged
write gates. Keep battery/raw/USB/split/RGB/Studio tests passing. Build affected
ARM variants and inspect actual wrapper call sites, memory and absence of native
BLE/watchdog. Review before a guarded LEFT-only update and verify saved settings,
exact application/resident identities and uninterrupted both-half health.

Use the existing Windows Bluetooth bond without deleting/replacing it. The
initial inventory has a powered adapter and one paired GMK70 BLE identity, but
no attached dongle. Distinguish actual Bluetooth connection/input evidence from
dongle protocol/selection tests; never claim receiver typing without a receiver.

Only restore the eight newly supported wireless bindings in the user's saved
map, preserving any newer Studio edits, layer names/order and the other five
unassigned positions. Back up a freshly clean map, save/reload/compare, leave
factory defaults unchanged and keep all personal layouts private. Sign commits,
check for secrets and push the existing development branch after verification.

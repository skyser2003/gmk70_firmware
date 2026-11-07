# Remapping GMK70 knob rotation in Studio

The firmware exposes two extra ordinary keys in unchanged ZMK Studio. They
appear directly above the knob's click button, with the same left/right
direction order as Vial:

| Position | Rotation | Default on Windows/Mac base layers |
| --- | --- | --- |
| 72, upper-left extra key | Counterclockwise | Volume down |
| 73, upper-right extra key | Clockwise | Volume up |

Select either extra key, choose a binding and save as for any other key.
Assignments are independent on each of the four layers. The Fn defaults are
transparent, so they inherit the base rotation action. The existing knob press
and all original physical positions 0–71 retain their assignments and relative
spacing.
A firmware update is needed to add the positions; later edits need only Studio.
No Studio source changes or sideloaded layout are required.
The layout reserves a top row for the rotation controls. Existing physical and
rotation assignments keep their positions and values.

## Implementation

Pins B7/B6 come from the authoritative QMK board JSON through the existing pin
generator, using QMK's four-edge resolution when none is specified. The left half samples quadrature
transitions through GPIO edge interrupts. A bounded 16-detent queue preserves
opposite-direction ordering; overflow drops the newest complete turn. Invalid
two-bit transitions discard an ambiguous partial turn. If IRQ setup fails, the
existing maintenance cycle supplies a polling fallback.

The scanner emits press/release events on virtual row 10, columns 0/1, which
map to the two appended positions. Each press lasts at least 5 ms, followed by
a 5 ms gap; no new worker or stack is added. Disabling callbacks or leaving a
valid output mode finishes the active release and discards queued motion. The
right half never emits left-encoder events. A settled physical matrix can stay
in its interrupt-triggered idle state while knob turns are processed.

These are taps. Hold-dependent actions are limited by the short pulse; the
firmware does not make a turn into a physically held switch. Key, media, layer
and other available Studio bindings follow normal ZMK behavior. Physical
direction, detent feel and USB/Bluetooth/dongle reception require hardware
confirmation.

## Saved maps and compatibility

The map grows from 72 to 74 positions per layer; the first 72 never move. The
compiled defaults initialize the new positions before existing saved bindings
load. The settings schema and flash partitions do not change. New position
names use the already supported literal encoding; existing record encodings stay unchanged.
the in-RAM settings index gains eight entries for the eight new bindings.
Existing VIA-to-ZMK conversion still imports the 72 physical positions and
leaves the added rotation positions alone.

All upstream dependencies and the VIA definition stay unchanged. The split
wire protocol, resident ABI, updater gates and permanent no-watchdog policy
remain unchanged. The universal image can update both halves; only LEFT owns
the physical encoder and saved Studio map. No device operation is part of a
build or test.

Native tests cover decoding, bounce, opposite turns, queue overflow, actual
GPIO callbacks, queued pulses, disable/mode releases and timestamp wraparound.
Settings tests append the rotation bindings to an existing four-layer store,
reload the original bindings, and exercise subsequent compactions.

See the [offline validation](validation/2026-10-31-zmk-knob-keys/README.md).

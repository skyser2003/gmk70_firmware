# Repository custom feature parity

These additions synchronize project-specific behavior, independently of native
framework features and restored stock GMK70 controls. They do not change saved
settings formats, dependency pins, resident installers or custom keycode values.

## VIA and Vial: awake idle matrix scanning

The shared board adapter wraps native matrix scanning. Once local raw and
debounced keys are released and quiet, it drives the populated columns low with
open drains and arms falling-edge row interrupts. Idle calls retain native
`matrix_post_scan()`: split transactions, peer-only presses, disconnect releases
and board hooks continue. Encoder maintenance and other framework tasks continue
in their native paths. A pending row edge resumes native scanning and debounce.

Interrupts are armed before driving the columns, followed by a row sample so a
key pressed during arming cannot depend on another edge. Existing EXTI ownership
causes polling fallback. Matrix initialization restores normal scanning. An
explicit low-power hook yields awake IRQ ownership before STOP preparation;
after resume, dormant stock wake callbacks are retired before row IRQs are
re-enabled. This includes split RX, which shares C13's interrupt group.

This removes local idle GPIO sweeps; it does not replace the existing board
sleep policy or claim measured battery-current savings. Upstream framework
sources remain unmodified.

## VIA and Vial: pairing hold ownership

Pending three-second pairing holds now belong to a physical matrix position.
Releasing another key with the same selector binding cannot cancel the newer
hold. The early physical-release hook cancels a hold even when the key has been
remapped or another feature consumes its release. Synthetic selectors can select
a host but cannot establish a physical pairing hold.

Live matrix keymap writes, buffer writes and resets cancel a pending hold through
the native storage boundaries. Cancellation is conservative, including edits to
other positions. Native bounds, Vial authorization and physical switch gates
remain authoritative; the expiry callback checks the physical mode again.

## VIA and Vial: radio report recovery

Accepted connection changes latch recovery for the normal keyboard task; the
radio parser does not invoke native report APIs. Actual loss discards native
keyboard/modifier state and custom flip ownership, drains later releases and
blocks new disconnected key presses. Empty transport reports also clear host
state when native report caches remember a release that pairing suppressed.
Repeated status and same-channel wake preserve live held input; actual host
changes clear ownership before selecting a new destination.

Both-low physical selector input is explicitly invalid. It cannot inherit an
old logical Bluetooth channel and accidentally pass a physical-mode gate.
The no-selector policy remains separate.

## ZMK: native map replacement and RGB saves

Individual edits, Studio Discard/Reset and layer move/add/remove/restore cancel
physical holds and queued/pending pairing before native work begins. Slow,
failed or no-op native operations cannot race the old pairing deadline. Native
arguments/results and settings behavior remain unchanged; a command already
transmitted to the radio cannot be retracted.

The native RGB callback retains its exact saved bytes and 60-second debounce.
Its latest snapshot remains pending through wireless mode, USB loss, storage
contention and failed writes. The existing update worker schedules a separate
system-work save; the storage worker rechecks LEFT, physical wired mode,
configured USB and service/storage ownership at execution. Retry is bounded to
once per second. Native startup behavior-ID writes retain their existing
pre-enumeration route. No RGB schema, bool-setting API or thread is added.
Latched storage faults retain the existing fail-closed recovery policy.

## ZMK: held shortcut modifiers

Mapped function-flip shortcuts keep independent modifier contributions through
unrelated native events and overlapping shortcut releases. Report copies merge
them with native explicit/implicit state for NKRO, six-key projection and USB
boot. Only custom mapped events bypass native singleton implicit updates;
unaffected native events keep their pinned semantics. Native clears cancel
custom ownership, including with the radio endpoint disabled. See
[function/media flip](ZMK-FUNCTION-FLIP.md) for the native HID/listener tests.

## ZMK: physical LED power control

The existing serialized pixel owner keeps both local LED rails off initially.
A nonzero frame enables the rails with the data pin low, allows a separate
1 ms supply-startup guard, then sends the frame. Already-powered transfers
retain the 280 us data-reset interval. A successful black frame completes DMA/SPI
and the low latch interval before switching the rails off. The unpowered data pin stays low.
An already-dark, unpowered frame needs no transmission. Failed transmissions do
not pretend the rails have been switched off; a subsequent successful retry can
complete shutdown.

The same behavior applies to both physical roles. Held battery/charging overlays
can temporarily light an otherwise disabled RGB frame and restore physical
darkness when released. Native saved RGB enable/effect/brightness settings and
split replay remain unchanged.

## Verification boundary

The current [consistency validation](validation/2026-11-01-custom-behavior-consistency/README.md)
adds the radio/native-cache, native map-replacement, RGB callback/backend and
native HID/listener regressions described above. All mandatory suites and 19
builds pass. The canonical Studio image is 85,456 bytes and uses 28,200 of 28,544
RAM bytes, leaving 344 bytes; both retained regions and all resident bytes remain
preserved. The following paragraphs retain the preceding parity/hardware history.

Dedicated native regressions execute both pinned frameworks' actual matrix,
debounce and split-post-scan code with simulated GPIO/IRQs, plus native deferred
timers and native keymap/storage routines for pairing. Pixel tests simulate the
real MMIO driver and exercise battery overlays through the real split lighting
path for both half sizes. The standard build/test commands do not access devices.

The preceding parity batch passed `make test`, `make test-zmk`, `make check-tools`,
formatting and all 19 affected firmware builds. ARM checks confirm the final
matrix/STOP integration in all twelve QMK/Vial images. All fourteen application
packages retain the pinned resident checksums. After integration with `7a8d7da`
(Studio knob keys) and the held-indicator refresh correction, the canonical ZMK
Studio image uses 28,144 of 28,544 RAM bytes, leaving 400 bytes, with both retained
regions excluded. Focused tests
check that LED power cycling preserves the encoder's B6/B7 GPIO configuration
and that battery overlays at virtual knob positions 72/73 release power correctly.

The first hardware check found that LEFT's initial indicator frame after LED
power restoration was not visible: held indicators worked with RGB on and stayed
lit when RGB was turned off during the hold, but a new hold from off remained
dark on LEFT. RIGHT's repeated peer frames still displayed the level. The driver
distinguishes supply startup from data reset, but a 1 ms guard alone did not fix
the physical test. Pressing both indicator keys supplied another frame and made
LEFT display correctly, including after either key was released.

Held indicators now refresh locally every 50 ms, matching native animation
cadence even while upstream RGB is off. This removes the dependency on a single
unacknowledged LED frame after supply restoration. Releasing the final hold
restores darkness and stops the extra local refresh. Native tests cover the
unchanged held frame being sent again, both indicator modes, and no continued
local transmissions after release. The corrected held-refresh image subsequently
passed the user's typing, RGB, knob and individual RGB-off indicator/release
checks. This consistency batch has no new hardware acceptance; LED rail rise
time remains unmeasured.

Software tests cannot establish first-key latency on silicon, STOP recovery on
the keyboard, rail settling, electrical leakage/backpower or battery-current
savings. Those remain separate hardware acceptance checks for these changes.

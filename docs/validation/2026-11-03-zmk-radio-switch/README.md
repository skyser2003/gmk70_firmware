# Bluetooth profile and connection-mode transition correction

The user reported lag and then loss of typing after a short Bluetooth-profile
shortcut from an already paired device to another. Switching back did not
recover; moving the physical mode slider to wired USB restored operation without
powering off. The shortcut triggered the problem; the slider was the recovery.

The user subsequently clarified that one character repeated as though held
before typing stopped. The [stuck-key follow-up](STUCK-KEY-ANALYSIS.md) separates
memory tests, host-state fault injection and residual transport limits. It also
records a new clear-routing candidate that has not been flashed.

## Evidence and correction

Read-only radio status after recovery showed BT1 selected/active/connected with
96 accumulated radio errors, 129 SELECT transmissions and 19,655 reports. These
aggregate counters do not identify which error caused the incident. Battery and
updater inspection were unavailable while the physical selector was wireless;
that authorization boundary was not evidence of a stalled USB worker.

Three source-level probes reproduced real defects in the 105,972-byte baseline:

- A newer selection could be erased by the old SELECT's ACK timeout: desired
  BT2, active BT1, faulted, with no pending selection.
- CONNECTED received without a separate SELECT ACK still blocked HID for the
  retry window and then permanently faulted the connected destination.
- Changing destination could continue retrying an obsolete PAIR operation.

The correction preserves the latest request through old-frame success/failure,
retires obsolete control ownership, suppresses obsolete pairing retries and lets
an attributed CONNECTED recover a fully transmitted SELECT. Partial/failed UART
frames do not gain that recovery context. Claimed frames remain immutable.
A 20 ms quarantine consumes late generic ACKs before another control owns them;
it does not establish attribution for arbitrarily delayed untagged messages.
The existing 5.5-second USB battery-sampler recovery interval remains intact.

Maintained regressions cover actual short-press/release radio behaviors through
pinned native keymap APIs; BT1 to BT2 and back; rapid repeated selection;
slider transitions across USB/Bluetooth/dongle; transient contact; partial TX;
old/new ACK ownership; late CONNECTED; and timer rollover. The new cases failed
before implementation and pass afterward. Existing native radio, battery-UART,
power and framework suites also pass. An independent source review found no
material regression.

## Build and preservation

All eight affected wired ZMK distributions compile. Full project tests, tool
format/lint/strict typing checks and relevant C formatting/static checks pass.
The canonical universal image is 105,900 bytes, CRC32 `2042255645`, SHA256
`897d6ea48bd14a9b119b009c8a78e0aee3985529358af7f4209aa036042331fa`.

Core state remains 228 bytes: two new flags use existing padding. Reordering
private byte/word metadata before the report queue reduces ARM instruction size
without changing a serialized interface. Total canonical RAM remains
28,162 / 28,544 bytes, including unchanged stack reservations. No features were
removed and no hardware watchdog was introduced.

Both halves now verify the exact canonical image and resident CRC32
`1770733249`. Four before/after joined-half health windows each exceeded 70
seconds with zero retries, reconnects or watchdog feeds. LEFT installation was
acknowledged. RIGHT received one INSTALL request with an unvalidated
acknowledgment; independent exact running-image/resident readback and its final
health window established completion. No request was replayed, and no manual
power cycle or right-only USB connection was needed. No radio firmware or
resident installer was written.

Four saved layers and 296 bindings verify unchanged before installation, after
LEFT and after RIGHT, including native behavior metadata and physical geometry.

An explicit SELECT-only hardware probe connected BT1, BT2, returned to BT1,
rapidly requested BT2 then BT1, and restored USB. All observed phases reached
their destination with a drained queue and zero accumulated radio errors.
There was no pairing request or synthetic key injection. These checks exercise
radio transition state rather than measuring physical typing latency.

The user then tested the original shortcuts and reported that typing works
initially. The original failure was intermittent after several switches, so
**extended-use stability remains unconfirmed**; the user is continuing normal
use. A subsequent read-only USB snapshot had no matching diagnostic interface
available, and no post-retest counters were inferred. See the
[sanitized hardware result](hardware-result.json).

Source regressions, a connected status and one initial manual pass do not prove
that every wireless failure has been eliminated. Physical battery-only STOP/wake,
current and overnight endurance remain outside this test.

The [three-stack audit](../../CUSTOM-FEATURE-PARITY.md) records other missing,
unsynchronized and intentional behaviors; those are separate from this fix.

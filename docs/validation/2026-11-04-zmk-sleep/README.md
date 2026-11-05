# Battery sleep handoff correction

The user reports that sleep still fails with USB completely disconnected,
although RGB switches off normally after 30 seconds. RGB idle-off demonstrates
that native inactivity is detected; it does not establish entry into WB32 STOP
or the external radio's low-power state.

Read-only inspection before this work verified both halves on the 105,900-byte
application, CRC32 `2042255645`, with resident CRC32 `1770733249` unchanged.
The keyboard was connected in wired mode for inspection. No battery-mode
current, sleep duration or previous overnight trace was available.

## Reproduced handshake failures

Two reachable interleavings can cancel sleep without any physical activity.

1. The RIGHT serial responder holds `wired_lock` while receiving a fragmented
   sleep RPC. A service poll calls the old full-idle getter, which treats that
   occupied mutex as non-idle. The resulting cached BUSY flag is returned by
   the very RPC that caused it, and LEFT cancels preparation. A two-thread
   probe using the real power logic and RPC transactions 13–16 produced 100
   BUSY replies and 100 cancellations. The corrected admission produced zero
   BUSY replies or cancellations under the same schedule.
2. LEFT releases the wire mutex after a final sleep RPC. The higher-priority
   scanner can immediately acquire it and begin a normal matrix poll before
   the service owner pauses the wire. The final wake check sees that new UART
   traffic and cancels sleep. A real-driver scheduler probe reproduces the
   extra transmission, occupied lock and wake latch without physical input.
   With ordinary polling stopped during preparation, all three disappear;
   a genuine matrix wake still latches.

These reproduce software defects, not their historical frequency on the user's
keyboard. The earlier hypothesis that a paused scanner's brief mutex section
alone starves LEFT is not supported by the single-core priority schedule: those
sections do not yield. An artificial native-callback starvation probe must not
be presented as proof of that hardware cause.

## Correction and preserved gates

Physical input admission now uses an IRQ-protected snapshot of local matrix,
pending scan and encoder state. It does not mistake required peer traffic for
a held key. The original full mutex/UART-idle checks remain in wake preparation
and wireless runtime-storage admission. Encoder IRQs are excluded while taking
the snapshot; scanner mutations do not yield in their relevant sections.

Preparing sleep also stops ordinary LEFT matrix polling, with both the early
and under-mutex checks. Explicit power/control RPCs remain available until the
final handshake completes. Cancellation resumes background polling. Local
input scanning and wake detection remain active until the existing final pause;
real key, mode, cable and RX wake checks are preserved.

Native ZMK activity, suspend/resume and the existing WB32 STOP implementation
remain in use. The default sleep timeout is five minutes, and RGB idle-off
remains 30 seconds. Permanent firmware still never initializes or feeds a
hardware watchdog. No settings schema, saved layout or resident is changed.

The candidate also includes the previously unflashed radio-clear routing fix
from the [stuck-key investigation](../2026-11-03-zmk-radio-switch/STUCK-KEY-ANALYSIS.md).

## Validation

Both maintained regressions failed before the changes and pass afterward:
RIGHT's peer-traffic probe no longer advertises input BUSY, and preparation
suppresses background polls while retaining explicit control RPCs. Native
power, real wired-driver, genuine-wake, USB-power, critical-battery and
held-key tests pass. An independent native activity simulation still enters
STOP at 301 seconds for the default five-minute timeout.

All eight affected wired distributions compile. `make test`, tool checks,
strict typing, edited C formatting and relevant static checks pass. The
canonical universal image is 105,996 bytes, CRC32 `3957255507`, SHA256
`88a1c6490e8543201a29b73fa13bd079d5d1c0bfbf8ef26dad8b716c492fd1b2`.
It uses 28,162 / 28,544 RAM bytes, including the unchanged 14,336 stack bytes.
The separate radio-qualification image uses 106,464 / 106,496 flash bytes;
no feature or stack reservation was removed to fit the correction.

Private evidence, probes, build/test logs, clean Studio backup, frozen source
and single-target installation cases are under `.work/sleep-investigation/`.
The user's subsequent battery-only functional wake result is recorded below.
Successful offline STOP entry, an update over powered USB or a functional wake
does not measure low sleep current or establish overnight endurance.

## Installed image verification

Both halves now verify the exact 105,996-byte image and resident CRC32
`1770733249`. One INSTALL was requested and acknowledged per half. Four
before/after both-half health windows each exceed 70 seconds, with zero
transport retries, relay retries, reconnects or watchdog feeds. No manual
reconnect, direct-right USB connection or recovery procedure was used.

Read-only Studio snapshots before installation, after LEFT and after RIGHT are
identical, with four saved layers and 296 bindings and no unsaved changes.
No radio firmware, resident installer or saved layout was written. Installation
health alone does not qualify battery sleep.

A fresh read-only pre-test battery sample was valid at 6%, with charging
present and 256 replies for 256 queries. The user was asked to charge further
before the six-minute battery-only test so critical-battery shutdown would not
confound idle-sleep observations. The percentage at test start was not captured.

## User's battery-only wait and wake test

The user reported completing the requested intentional idle wait, then explicitly
confirmed that a keypress resumed Bluetooth typing normally before reconnecting
USB. The exact wait duration was not independently recorded.

The read-only watcher detected USB absence but timed out before obtaining a
verified wired return within its 20-minute return window. It produced no final
observation file. Consequently it supplies no exact reconnect timestamp, measured
USB-absence duration or sleep-entry reason; its timeout does not itself prove
that USB remained physically absent for the whole window.

The first subsequent read-only snapshot found LEFT uptime 37,015 ms and RIGHT
37,184 ms, with the expected application and resident CRCs on both and zero
watchdog feeds. Both recent boots are consistent with the installed STOP/wake
path, which restarts the applications after wake. Combined with the user's
Bluetooth key-wake result, this is positive functional sleep/wake evidence.
It is not direct proof of STOP residence time, the precise sleep-entry trigger,
radio sleep current or overnight battery life.

The post-test Studio snapshot is identical to the post-installation snapshot:
four saved layers and 296 bindings, with no unsaved changes. Evidence is in
`health-after-user-wait.json`, `studio-after-user-wait.json` and `watch.log`
under the private evidence directory. No firmware write or software reset was
sent during these observations.

An additional both-half health observation after wake verified more than 70
seconds of continuous uptime, with zero transport/relay retries, reconnects or
watchdog feeds and the exact expected applications/residents. Its private record
is `post-wake-health.json`. The [sanitized result](wake-result.json) records the
functional pass and the measurement limits.

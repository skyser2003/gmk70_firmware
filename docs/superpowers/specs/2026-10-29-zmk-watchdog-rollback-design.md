# LEFT watchdog rollback experiment

The user requests tracing the QMK/Vial regression, removing its shared addition,
and testing the original reset path in ZMK. Continue on the existing development
branch in this sole maintained repository; preserve RIGHT, residents and settings.

## Evidence and hypothesis

The receiver-only Vial update at `a6a59a2` returned after USB re-enumeration.
The execution extension added pre-CRT role sampling and the independent watchdog
(`fa7f351`). Its initial GPIO-before-APB-clock defect was corrected; native ZMK
already enables APB source clocks before GPIO. Later Vial installation still
failed automatic restart after trial watchdog recovery was introduced
(`d138d60`, right checkpoint `5308274`). Native ZMK adopted the same watchdog
register contract in `65cd79f`, and starts it on every permanent boot.

The remaining shared watchdog state is a hypothesis, not a proven common cause.
Do not revert the corrected GPIO ordering or remove working keyboard features.

## Approach

Use two separately labeled LEFT Studio diagnostic variants with watchdog init
and feed producing no MMIO at all. They report an explicit health flag (bit 4),
zero feeds and no watchdog-ready flag. Only local LEFT USB/wired update writes
are allowed by the diagnostic exception; peer writes remain unavailable. The
ordinary readiness gate and all ordinary builds remain unchanged. The initial
diagnostic loads existing settings and denies settings writes. After the successful
hardware-reset comparison, the final opt-in build restores settings saves under
the same LEFT/wired/worker/journal gates, without requiring a disabled watchdog.

The diagnostic retains the native receiver capability footer so the installed
handoff firmware can bootstrap it without SYSRESETREQ. It does not include the
outgoing handoff: its updates use the original hardware reset path. The current
watchdog cannot be stopped by undoing software; allow its expiry, then require
70 seconds of uninterrupted zero-feed diagnostic health before any next write.
If automatic recovery fails, stop writes and request at most a joined-LEFT cycle.

After stable bootstrap, update diagnostic A to distinct B with one INSTALL and
bounded read-only return observation. Initial recovery plan: if successful, update back to the frozen working handoff
image through the diagnostic's hardware reset. Execution ruling after A-to-B
success: leave the requested watchdog removal installed, restore Studio saving
in a final no-watchdog image, and test B-to-final through hardware reset. The
frozen old handoff remains recovery material only.
Record ACK separately from installed identity, health and automatic return.
If a reset fails, retain the failed result and investigate before more writes.

Alternatives: reverting all trial integration would remove unrelated features;
adding another reset workaround would not test the user's hypothesis. A labeled
single-variable watchdog rollback provides the useful comparison.

## Limits

No hardware-reset failure location has been captured. Passing with no watchdog
would isolate watchdog-dependent behavior, not prove its silicon mechanism.
No RIGHT update, resident/radio/option-byte write, release publication or general
watchdog removal is authorized by this experiment. Never replay an INSTALL.

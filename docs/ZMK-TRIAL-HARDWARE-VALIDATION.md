# Live staging-receiver validation

Validated 2026-10-27, with explicit user authorization to update and test the
USB-connected keyboard. This covers the opt-in Vial trial receiver described in
[ZMK-TRIAL-LAUNCHER.md](ZMK-TRIAL-LAUNCHER.md). No trial program was executed and
no independent-watchdog recovery was tested; the CPU handoff remains disabled.

## Installed software and preservation

Both halves initially ran published Vial 0.1.7, application size 91,564 bytes,
CRC32 `0xA63F205B`. Their resident CRC32 was `0x698B3EC1` and both updater
receivers were idle. The native board UID and left-master/right-slave roles
matched the expected hardware.

The trial branch was merged with `codex/vial-status-release` before building,
retaining the current custom labels and RGB-off battery-indicator fix. The
installed trial-updater application comes from source commit `a6a59a2`:

- Application: 92,548 bytes at `0x08002000`; CRC32 `0xE3A6B07E`.
- Application SHA256: `a864a0b014fd7240d5a84cadff93c8816b2969ecbea9b16c6f83bc888793b5b2`.
- Resident package SHA256, padded to 8 KiB:
  `be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`.

The ordinary application updater installed and verified the right first through
left USB, then installed and verified the left after USB re-enumeration. It did
not program the resident. Resident INFO CRC32 remained `0x698B3EC1` throughout;
application INFO size/CRC matched the new package on both halves after install,
after staging tests and at final verification.

A fresh native Vial backup was captured before updating, SHA256
`cbf580393ef6287e2148707b0fe8db666e1613fcd4467c6ba90bde8ccaca9fe0`.
The user completed native physical unlock before installation and again after
the left restarted. No insecure unlock or synthetic matrix input was used.
After the update, ten keymap getter blocks needed restoration. Final comparison
of all 263 distinct saved configuration getters matched the original backup:
360 key positions, eight encoder assignments, 16 macros, 128 dynamic entries,
26 QMK settings and three board settings. Native Vial was explicitly relocked.
The installed embedded Vial definition also matched the frozen package.

## Live checks through left USB

All six groups passed independently on each half (12 groups total):

| Check | Observed behavior |
| --- | --- |
| Link/RAM contract | Application-linked load address or RAM ceiling `0x20007000` rejected as BAD_IMAGE. |
| Incomplete transfer | A partial upload could not SEAL; ABORT returned the receiver to idle. |
| Corruption | A fully transferred 1,024-byte image with an intentionally corrupted byte failed CRC validation. |
| Valid reset-return fixture | Upload and SEAL passed; duplicate DATA was idempotent and incorrect offsets were rejected. |
| Valid hang fixture | Upload and SEAL passed with the same bounds/duplicate checks. |
| Session expiry | An unfinished session expired after an 11.2-second wait; stale DATA was rejected. |

For each valid fixture, ordinary-namespace ABORT was rejected while the trial
owned staging. Operation 5 in the trial namespace was unavailable. Trial RUN
returned UNAVAILABLE with the matching session and SEALED state, showing the
actual disabled engine response rather than accepting a temporary bridge error.
The current application/resident identities remained unchanged, then trial
ABORT returned the receiver to idle. The fixtures were uploaded and validated,
not executed. No ordinary INSTALL request was sent for a trial image.

After relocking, trial INFO remained readable for both halves while trial BEGIN
and RUN returned LOCKED at the USB authorization gate. Final ordinary INFO
reported idle receivers, correct roles and unchanged application/resident CRCs.

During right-side staging, the relay produced transient UNAVAILABLE replies. The one-time hardware client initially stopped on these, then adopted the
maintained updater's bounded idempotent retry behavior and correlation rules.
The complete passing run retried 52 such replies, all for the right target.
A polling STATUS failure must not be mistaken for RUN's expected UNAVAILABLE:
verify operation, session/offset and SEALED state before accepting that result.
The maintained offline `trial.py` remains an offline package/plan tool; this
hardware client was a separate, explicitly invoked device operation.

## Evidence and remaining limits

Local evidence (not committed because it includes recovery/configuration data):

- `artifacts/recovery/2026-10-27T132127Z-vial-before-update.json`.
- `artifacts/recovery/2026-10-27T133505Z-vial-restoration-verified.json` records
  restoration before final relock; its `vial_relocked: false` is intentional.
- `artifacts/recovery/2026-10-27-zmk-trial-staging-validation.json` records all
  passing operations, final configuration comparison and confirmed relock.
- `.work/device-updates/zmk-trial-2026-10-27/` retains the frozen application
  package, explicit device scripts and detailed temporary traces.

The merged source passed `make test`, `make check-tools`, QMK release/debug
normal/updater builds, Vial normal/updater/trial-updater builds, and both staging
fixtures. No build or offline test command performed a device operation.

This establishes live application installation, staging programming/validation,
authorization and configuration restoration through the left USB bridge. It
does not establish trial execution, watchdog return, recovery with broken UART,
brownout/power-loss behavior, full typing/RGB regression coverage, or any ZMK
kernel/driver support. The next implementation step remains the audited early
startup trampoline and WB32 independent-watchdog backend in the launcher design.

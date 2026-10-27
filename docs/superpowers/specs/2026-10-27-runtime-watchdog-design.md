# Runtime watchdog qualification

The previous pre-CRT trial build required physical right-half recovery. Before
resuming staging execution, qualify the independent watchdog from an otherwise
normally started Vial application. Keep the right on the recovered release while
testing the left through its existing USB cable.

The experimental `unlocked-trial-updater` gains an explicit `runtime-watchdog`
profile, with separate build caches and artifact names. It retains the upstream
reset handler and excludes the pre-CRT hook, raw role GPIO and staging handoff.
RUN is unavailable. Ordinary unlocked/updater distributions remain unchanged.
The resident, native settings schema, flash layout and wired/role gates remain
unchanged. Build and test commands never operate the keyboard.

Runtime PROBE acknowledges, waits 300 ms, records entry in retained RAM, disables
interrupts and starts the existing IWDG driver without feeding it. A verified
Cortex-M3 DWT cycle counter provides a 12-second software reset fallback. Failure
to enable the counter rejects the request; a counter that stops triggers a
bounded software reset. This protects a failed watchdog test, not arbitrary CPU
or bus faults. Only actual IWDG reset flags prove watchdog recovery.

An explicit FALLBACK_TEST operation exercises the same deadline while skipping
IWDG start. It must return with software reset flags. Runtime entry is restricted
to PROBE with zero image; the retained record stays 64 bytes and staging RUN's
software-reset admission remains unchanged.

Read-only CAPS (0x13) returns four words: capability bits, fallback milliseconds,
trial RAM base and end. Bits are PROBE=1, RUN=2, EARLY=4, FALLBACK=8,
FALLBACK_TEST=16. Runtime profile reports 25/12000 on the left and 0/0 on the right; firmware rejects right-side qualification requests. Full launcher reports 7/0.
FALLBACK_TEST (0x14) uses PROBE validation, authorization and result action=2.
CAPS echoes the fresh request nonce, like RESULT. Host tooling checks capability
support before any mutation and never reports software fallback as IWDG proof.

Offline checks cover counter wrap/stall/write failures, retained record
corruption and one-shot entry, unauthorized operations, staging RUN exclusion,
profile cache isolation, ELF startup boundaries and host result correlation.
Then independently review before any firmware write.

Device sequence: fresh native configuration backup; verify both resident CRCs;
install only left application; restore and verify that fresh configuration; run
left watchdog then fallback tests; verify both identities, residents and settings.
Do not update the right in this milestone. Keep the known-good recovery package.
No driver installation or resident writes are authorized by this work.

Primary register references: [Arm Cortex-M3 TRM, DDI0337H](https://documentation-service.arm.com/static/5e8e107f88295d1e18d34714)
and [Westberry WB32FQ95 reference manual](https://www.westberrytech.com/uploads/file/WB32FQ95xx/EN_RM2905025_WB32FQ95xx_V01.pdf).
DWT is optional; enable readback and actual counter advance are required.

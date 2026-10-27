# Staging execution and watchdog recovery

Current result: the [first actual left ZMK trial](ZMK-LEFT-FIRST-TRIAL.md) was
flashed, enumerated over USB, and returned automatically to Vial on 2026-10-27.
The right and both residents are unchanged; all 263 configuration checks pass.
The earlier checkpoints below explain the implementation and recovery work.

2026-10-27 continuation of the [receiver milestone](ZMK-TRIAL-LAUNCHER.md).
The user authorized firmware installation and trial execution, then explicitly
requested a separate permanently unlocked Vial release asset and its installation.
The standard unlocked updater was added to Vial 0.1.7 and initially installed on
both halves. The right still runs that 91,236-byte application, CRC32 1962194226.
The left now runs the experimental 95,772-byte trial updater, CRC32 44883505.
Resident CRC32 remains 1770733249 on both halves.
The normal locked release assets remain available.

The execution backend is confined to the opt-in `trial-updater` and
`unlocked-trial-updater` builds. The latter uses upstream `VIAL_INSECURE=yes`,
as requested. Standard unlocked firmware has no trial backend. Build and test
commands never operate a keyboard; the separate host runner performs device work.

## Memory and reset contract

Trial namespace 0x71 now requires header ABI 2. ABI 1 trial packages are rejected:
they do not declare the new RAM exclusion. Normal installation retains namespace
0x70, ABI 1 and the same resident bytes. Trial manifests use
`gmk70-staging-trial-v2`, staging address 0x08020000, RAM start 0x20000040 and
maximum initial stack 0x20006FC0. No trial writes an ordinary install commit.

The application reserves exactly 64 NOLOAD bytes at 0x20000000 for the retained
intent/result. Its other RAM allocations start at 0x20000040. The physical RAM
end remains 0x20007000, preserving QMK boot magic at 0x20006FFC. Putting the record
at the top of RAM would collide with the unchanged resident's stack.

RUN prepares a checksummed, role-bound record, waits 300 ms for the response,
clears old reset flags and requests a software reset. A strong, trial-only
Reset_Handler runs before CRT clearing and before framework clocks/peripherals.
It consumes a valid intent before sampling handedness B9 directly,
and revalidates the erased commit, staging vectors and complete image CRC.
Only an expected software reset (optionally also PINRST) admits a request.
Power/watchdog/other reset causes cannot relaunch it.

Watchdog setup clears the POR-only debug freeze, enables its bus/clock and LSI,
uses finite readiness waits, verifies prescaler/reload, and starts last. At nominal
32 kHz LSI, prescaler /32 and reload 2999 give three seconds. This is an engineering
setting; live return timing includes reset/startup/USB and split pause overhead.
Setup failure before start returns to Vial. During trial execution there is no
refresh path; failures await watchdog reset. After a qualified left trial return,
Vial configures a long recovery period and services a potentially inherited
watchdog. The observed runtime instability means reset must not be assumed to
stop it.

Handoff masks interrupts, disables SysTick/NVIC and clears pending/fault state,
sets staging VTOR, privileged MSP, PSP=0, CONTROL=0, BASEPRI=0, FAULTMASK=0 and
PRIMASK=1, then branches in assembly. Trial startup owns enabling interrupts.
Returning through LR requests a software reset. These initial fixtures have no
RAM initialization or peripheral drivers and are not a ZMK kernel port.

An entered record becomes a terminal RETURNED result on the next reset, recording
its actual reset flags. Intent consumption prevents a trial reboot loop. The
record is diagnostic RAM, not a power-loss-persistent journal.

## Protocol and split recovery

STATUS/BEGIN/DATA/SEAL/ABORT/INFO keep their previous operation numbers; RUN is
0x10. New WATCHDOG 0x11 is a watchdog-only probe from IDLE, with a nonzero request
nonce and no staging execution. RESULT 0x12 is read-only and remains available
while Vial is locked. All mutations still require native unlock in locked builds
and wired USB on the left master; right operations inherit master authorization.

RESULT bytes 16..31 contain four little-endian words: action | (phase << 8),
reset flags, image size and image CRC. Actions are RUN=1 and PROBE=2. Phases are
REQUESTED=1, ENTERED=2, RETURNED=3 and REJECTED=4. Reset bits are LOWPOWER=1,
WWDG=2, IWDG=4, SOFTWARE=8, POR=16 and PIN=32. PROBE and RESULT echo their request
nonce in bytes 8..11. INFO continues to identify the running application/resident.

The left pauses ordinary peer polling for eight seconds after forwarding RUN or
WATCHDOG. Updater RPC is allowed during the first 300 ms to collect the ACK;
after that all peer RPC is suppressed until the pause expires. Returning false
before upstream matrix transport releases remote held keys without incrementing
its private disconnect counter. Successful normal transport resumes its usual
RX flush/handshake afterward. The right watchdog has no UART dependency.

`trial.py` remains an offline package/plan tool. The separate `trial_run.py`
checks identities, uploads/seals before RUN, handles stale/transient responses,
and checks the terminal result and reset cause. A successful upload alone does
not establish CPU execution or recovery.

## Validation status

Portable C sanitizer tests cover the watchdog write sequence and failure paths,
record corruption/torn writes/reset causes/roles/one-shot behavior, peer pause
boundaries/timer wrap and actual updater authorization/ACK/reset scheduling.
The linked ELF check verifies the strong pre-CRT reset vector, retained NOBITS
section, RAM bounds, preserved boot magic, masked handoff and actual wrapper calls.

The first live execution-backend installation failed during right-half restart
verification, before WATCHDOG or RUN was issued. The 94,956-byte application
(SHA256 `63f967ae9aed361028430840ec6bdc77e14393ed126d3c7b24399122978b9af7`)
was transferred, but the right updater did not answer. The installer stopped and
the left remained on the verified 91,236-byte standard unlocked application.
Read-only follow-up confirmed left INFO and right timeout. The published standard
unlocked release asset is the earlier working build, not this failed trial build.

The source audit found that the early B9 sampler enabled GPIO gates while the
APB1 input clock remained disabled after reset. This is a concrete defect consistent
with the startup failure; no hardware fault PC was captured. The correction enables
and verifies APB1PRE.SRCEN before GPIO, restores clocks afterward, and admits a
checksummed software-reset intent before any GPIO callback. Regression tests fail
with the previous ordering and pass after correction. Ordinary boots and watchdog
returns now take a path with no GPIO accesses.

Recovery completed through the documented factory bootloader entry on October 27.
Windows initially detected `342D:DFA0` with missing-driver code 28. The user
installed the driver manually; no driver installer was launched by the agent.
The recovery first verified the resident and failed-image hashes and erased
ordinary commit page, then wrote only the known-good published unlocked
application at 0x08002000. All 262,144 bytes were read back and matched the exact
expected image; every byte outside the 91,236-byte application payload remained
unchanged, including the resident, staging and metadata. No protection change,
chip erase or reset command was requested by the recovery tool.

After the user restored normal wiring and left USB, both halves reported IDLE,
application size 91,236, application CRC32 1962194226 and resident CRC32
1770733249. Native Vial was unlocked, and all 263 configuration getter responses
through the left USB master matched the saved backup without configuration writes.
Evidence is recorded in
[`2026-10-27-right-unlocked-recovery.json`](../artifacts/recovery/2026-10-27-right-unlocked-recovery.json)
and
[`2026-10-27-recovery-running-verification.json`](../artifacts/recovery/2026-10-27-recovery-running-verification.json).

The corrected backend is built (95,188 bytes, SHA256
`528fc71bdf0f9cc77f8b2c64a06c6c0da8883a78f56221ecd74185b065d96344`),
but has not been installed. Full `make test`, `make check-tools`, locked and
unlocked trial builds and linked ELF checks pass for the correction. Earlier
QMK release/debug normal/updater and Vial normal/updater builds also passed.

Live watchdog-only, return and hang execution remain unproven. Coordinated
both-half launch, power-interruption trials, long-running ZMK firmware and native
ZMK split remain later work.

Hardware source: [WB32FQ95 reference manual](https://www.westberrytech.com/uploads/file/WB32FQ95xx/EN_RM2905025_WB32FQ95xx_V01.pdf),
particularly sections 7.1, 7.2.12, 7.3.16/24/28, 18 and 27.4.2, plus the pinned
vendor CMSIS/ChibiOS sources. No option bytes, radio firmware or resident code
are changed.

## Runtime watchdog qualification

To reduce the chance of repeating right-half ROM recovery, the next profile
retains ordinary upstream Vial/ChibiOS startup. Build it explicitly with
`make build-vial-watchdog-qualification`. Its separate cache and artifact suffix
is `unlocked-trial-updater-runtime-watchdog`; it is experimental and is not a
published normal Vial asset. There is no early hook, raw role sampler or staging
handoff in this ELF. RUN remains unavailable even for a sealed staging image.

Read-only CAPS 0x13 returns capability bits, fallback milliseconds, RAM base and
RAM end in bytes16..31, and echoes a fresh nonce in bytes8..15. Bits are PROBE=1,
RUN=2, EARLY=4, FALLBACK=8, FALLBACK_TEST=16. The runtime profile reports25/12000
on the left and0/0 on the right. Both protocol processing and platform preparation
reject right-side qualification. The existing eight-second peer pause has not
been qualified for a twelve-second right fallback; that work is deferred.
The full pre-CRT launcher reports7/0. Host tooling requires CAPS before mutation.

Runtime PROBE enters an empty-image retained record after normal startup, masks
interrupts and starts IWDG without feeding it. A DWT counter is enabled and
verified before ACK; a12-second deadline or1024consecutive stalled reads causes
software reset if IWDG does not expire. FALLBACK_TEST0x14 uses the same deadline
while deliberately skipping IWDG start. Only reset flags4/36 prove IWDG;
flags8/40 prove the software fallback. Runtime ordinary initialization completes
the result record but never starts either timer or touches role GPIO. This guard
does not recover arbitrary CPU/bus faults in early kernel experiments.

Offline validation passed: sanitized portable/adapter tests,77repository tests,
77firmware-update tests, Ruff/strictmypy, QMK release/debug builds, Vial normal,
locked/unlocked updater, full trial profiles and the runtime profile. Actual ELF
checks also pass with Python optimization enabled. Independent review found the
right peer timing gap; regression tests first reproduced it, then verified the
left-only capability, processing and platform gates. The hardware results below
are narrower than the passing offline checks.

Design and execution checklist are in
[the runtime design](superpowers/specs/2026-10-27-runtime-watchdog-design.md) and
[the plan](superpowers/plans/2026-10-27-runtime-watchdog.md).

### Live results and final device state

The source at `9ca44e2` produced the runtime application:94,908bytes,
CRC32 3394405425, SHA256
`4b812d28a71444743dd9fa4997618d0ca8254b0b6bb528d58f089d975eb87f00`.
Only the left application was installed. Normal startup, both resident CRCs,
right identity and native settings restoration were verified before testing.

- WATCHDOG PROBE at16:48UTC received CAPS25/12000 and a terminal ACK, then USB
  disappeared. Recovery did not return within54.312seconds. The bounded software
  fallback did not recover this execution path. The user unplugged/reconnected
  left USB; Vial returned, but the retained record was cleared by the power cycle.
- FALLBACK_TEST at16:51UTC, on the same application, skipped watchdog setup and
  returned automatically in13.875seconds. RESULT was action2, phase3(RETURNED),
  reset flags40(SOFTWARE|PIN), with zero image size/CRC. Application and resident
  identities matched before/after. This proves the DWT/software reset path only.
- At16:54UTC the left was restored through left USB to the exact published
  standard unlocked0.1.7 application. No right application, resident, radio or
  driver was written. At16:55UTC both halves reported IDLE,91,236bytes,
  appCRC1962194226 and residentCRC1770733249. All263native configuration getter
  responses matched the fresh16:33UTC backup. No physical bootloader entry,
  shorting pads or right-only USB flashing was needed in this milestone.

The watchdog-dependent path remains unsafe to rely on. Its exact stall/reset
cause is not established; no hardware fault PC or failing MMIO address was
captured. A successful software-only deadline does not prove recovery from a CPU
fault or stalled bus access. Do not repeat live WATCHDOG or staging RUN until
watchdog setup/fault state can be captured and its recovery re-evaluated.
Right runtime qualification remains disabled. No ZMK/staging code was executed.

Sanitized evidence:
[qualification and final state](../artifacts/recovery/2026-10-27-runtime-watchdog-qualification.json),
[watchdog failure](../artifacts/recovery/2026-10-27-left-runtime-watchdog.json),
[software fallback success](../artifacts/recovery/2026-10-27-left-runtime-fallback.json),
[final configuration verification](../artifacts/recovery/2026-10-27-runtime-watchdog-final-verification.json).
Private native keymap/macro backups remain ignored.

### Recovery servicing qualification (2026-10-27)

The fault-capture runtime build returned automatically from the deliberate UDF
with software-reset flags and the expected fault PC. Its IWDG probe then returned
one matching RESULT with IWDG reset flags, but USB disappeared again afterward.
That instantaneous result did not prove stable recovery. A left USB power cycle
restored Vial and 35 seconds of read-only identity checks passed; the right and
both resident identities remained unchanged.

The full launcher now handles the possibility of an inherited running IWDG.
Only a valid left ENTERED/RETURNED record with a current software/IWDG reset
(optionally PIN) qualifies. Before CRT it configures the longest watchdog period
and reloads without issuing a start key. Vial initialization repeats that setup,
clears the current reset flags, and enables reloads in updater housekeeping.
Ordinary, power, corrupt, rejected and right-side recovery records do not touch
the watchdog. New trial execution configures its own short deadline and receives
no housekeeping feeds. Failed recovery setup prevents a new trial request.

Portable MMIO and native full-adapter tests exercise these gates, current versus
retained reset flags, and an admitted staging RUN through start and handoff. The
host now requires 30 seconds of continuously verified INFO and terminal RESULT;
transport retries restart that observation window. The revised launcher
subsequently passed the pre-CRT probe, staging return and
hang fixtures, and actual ZMK USB trial with stable Vial return. Exact watchdog
persistence is still unmeasured; the [live evidence](ZMK-LEFT-FIRST-TRIAL.md)
records the observed recovery behavior.

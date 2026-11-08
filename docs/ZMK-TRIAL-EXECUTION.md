# Staging execution and watchdog recovery

Execution is confined to the opt-in Vial `trial-updater` and
`unlocked-trial-updater` builds. Standard unlocked firmware has no trial
backend. Build/test commands never perform device operations.
See the [launcher guide](ZMK-TRIAL-LAUNCHER.md) for packages and offline tools.

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
refresh path; failures await watchdog reset. After a qualified trial return on
either role, Vial configures a long recovery period and services a potentially inherited
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

## Runtime watchdog qualification

The runtime qualification profile retains ordinary upstream Vial/ChibiOS startup. Build it explicitly with
`make build-vial-watchdog-qualification`. Its separate cache and artifact suffix
is `unlocked-trial-updater-runtime-watchdog`; it is experimental and is not a
published normal Vial asset. There is no early hook, raw role sampler or staging
handoff in this ELF. RUN remains unavailable even for a sealed staging image.

Read-only CAPS 0x13 returns capability bits, fallback milliseconds, RAM base and
RAM end in bytes 16..31, and echoes a fresh nonce in bytes 8..15. Bits are PROBE=1,
RUN=2, EARLY=4, FALLBACK=8, FALLBACK_TEST=16. The runtime profile reports 25/12000
on the left and 0/0 on the right. Both protocol processing and platform preparation
reject right-side qualification. The existing eight-second peer pause has not
been qualified for a twelve-second right fallback; that work is deferred.
The full pre-CRT launcher reports 7/0. Host tooling requires CAPS before mutation.

Runtime PROBE enters an empty-image retained record after normal startup, masks
interrupts and starts IWDG without feeding it. A DWT counter is enabled and
verified before ACK; a 12-second deadline or 1024 consecutive stalled reads causes
software reset if IWDG does not expire. FALLBACK_TEST 0x14 uses the same deadline
while deliberately skipping IWDG start. Only reset flags 4/36 prove IWDG;
flags 8/40 prove the software fallback. Runtime ordinary initialization completes
the result record but never starts either timer or touches role GPIO. This guard
does not recover arbitrary CPU/bus faults in early kernel experiments.

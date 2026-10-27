# Staging execution and watchdog recovery

2026-10-27 continuation of the [receiver milestone](ZMK-TRIAL-LAUNCHER.md).
The user authorized firmware installation and trial execution, then explicitly
requested a separate permanently unlocked Vial release asset and its installation.
The standard unlocked updater was added to Vial 0.1.7 and installed on both halves;
all 263 native configuration getters match the fresh backup. Its application is
91,236 bytes, CRC32 1962194226. Resident CRC32 remains 1770733249 on both halves.
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
Setup failure before start returns to Vial. After start there is no refresh path;
all failures await watchdog reset. The manual states that system reset stops IWDG.

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

The right requires the documented factory bootloader entry for recovery because
its application cannot receive relay commands. The prepared recovery restores the
already verified published unlocked application at 0x08002000. It first checks the
resident and failed-image hashes, then compares complete before/after flash images,
requiring every byte outside the application payload to remain unchanged. It never
requests protection changes or a chip erase. Physical bootloader entry is pending. Direct USB exposed neither Vial nor DFU
in normal mode, standalone mode, or after reconnecting USB in standalone mode.
The user currently lacks a pad-bridging tool. The working left application was
not replaced. The corrected backend is built (95,188 bytes, SHA256
`528fc71bdf0f9cc77f8b2c64a06c6c0da8883a78f56221ecd74185b065d96344`),
but has not been installed. Full `make test`, `make check-tools`, locked and
unlocked trial builds and linked ELF checks pass for the correction. Earlier
QMK release/debug normal/updater and Vial normal/updater builds also passed.

Live watchdog-only, return and hang execution remain unproven. Coordinated both-half launch, power-interruption trials,
long-running ZMK firmware and native ZMK split remain later work.

Hardware source: [WB32FQ95 reference manual](https://www.westberrytech.com/uploads/file/WB32FQ95xx/EN_RM2905025_WB32FQ95xx_V01.pdf),
particularly sections 7.1, 7.2.12, 7.3.16/24/28, 18 and 27.4.2, plus the pinned
vendor CMSIS/ChibiOS sources. No option bytes, radio firmware or resident code
are changed.

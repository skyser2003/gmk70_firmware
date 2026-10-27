# Staging trial launcher: software milestone

2026-10-26. Implements the first offline milestone of [the handoff](ZMK-LOCAL-HANDOFF.md).
The opt-in Vial trial updater can receive and validate a trial. RUN is deliberately
UNAVAILABLE: no watchdog register writes, reset trampoline or CPU handoff exists
in this milestone. Nothing here establishes recovery on a keyboard.

[Live staging validation on 2026-10-27](ZMK-TRIAL-HARDWARE-VALIDATION.md) now covers
application installation, receiver operations and authorization on both halves.
CPU handoff and watchdog recovery remain unimplemented and untested.

## Design and alternatives

Keep the resident and ordinary install ABI unchanged. Replacing the resident
would discard the installed recovery baseline; ordinary INSTALL would overwrite
Vial. A separate staging protocol preserves Vial and is the chosen development
path, as requested in the handoff. Initial trial execution must wait for the
independent watchdog integration described below. A software timeout cannot
recover a kernel that hangs with interrupts disabled.

The opt-in `trial-updater` mode shares the normal updater's page buffer and RPC.
HID packets retain command 0x7F but use namespace 0x71 (ordinary install is 0x70).
Header ABI is 1. STATUS/BEGIN/DATA/SEAL/ABORT/INFO retain operation numbers
1/2/3/4/6/7; trial RUN is 0x10. Operation 5 (INSTALL) is invalid in this namespace.
BEGIN's 16-byte payload contains size, CRC32, link address 0x08020000 and RAM
ceiling 0x20006FC0, all little endian. Session and offset are as in the normal
protocol. Responses retain its layout. Trial INFO reports running Vial and
resident identity; ABI 1 trial firmware always rejects RUN as UNAVAILABLE.

BEGIN requires every byte of the ordinary commit page to be erased; even a torn
record blocks trials. It does not erase or invalidate any commit. Only DATA may
write, and only staging pages. Ordinary and trial transfers are mutually
exclusive until ABORT or the existing 10-second expiry. Failed transfers require
ABORT/expiry too. Normal INFO/STATUS remain readable. Native Vial unlock gates
all mutation requests at the wired direct-USB left master; the right slave
inherits authorization through the existing RPC. EEPROM/log queries stay absent.
No persistent settings schema, initialization or EEPROM magic behavior changes.
Native per-build Vial magic can still reset saved maps when a new application is
installed; export maps before any later separately authorized installation.

Images have a distinct `trial.json`/`trial.bin` identity and are linked directly
at 0x08020000, 1024..122880 bytes, multiple of four. Core exception vectors must
point inside that image in Thumb state; reset/NMI/HardFault are required. Stack
is eight-byte aligned and within (0x20000000, 0x20006FC0]. Trial linkers exclude
0x20006FC0..0x20006FFF, including resident boot magic 0x20006FFC. No extra 8 KiB
of the reported SRAM is assumed. Initial fixtures have no settings, flash,
watchdog refresh, deep sleep or interrupt-driven peripherals.

## Watchdog recovery contract for the next milestone

Prefer a reset-mediated early application trampoline over jumping out of the
running ChibiOS scheduler. Before that can be enabled:

1. Reserve a bounded `.noinit` launch/result record in the application AND trial
   linker layouts, outside data/BSS/heap/stacks and separately from boot magic.
   Bind it to image CRC, size, contract version, target role and integrity check.
   Accept it only after an intentional software reset, never POR or IWDG reset.
2. Revalidate staged bytes and the erased commit before arming a short independent
   watchdog. Use vendor-documented LSI/IWDG clock gates, reload and busy flags;
   bound all readiness waits. If preparation fails before watchdog start, remain
   Vial. After start, failures must wait for reset without refreshing it.
3. Reset into the unchanged resident, which boots the Vial application. Run the
   trampoline before framework clock/peripheral initialization and before RAM
   clearing can destroy the record. Consume and invalidate it BEFORE any trial
   branch. An IWDG reset must fall through to Vial without another trial.
4. Audit watchdog persistence across the intentional reset, inherited LSI/IWDG
   gates and debug freeze. If it does not survive reliably, arm it in the early
   trampoline before executing trial instructions. A successful watchdog reset
   must stop/reinitialize it so the unchanged resident and normal Vial startup
   have sufficient time. Do not assume this behavior from another MCU family.
5. Handoff from privileged thread mode with documented clock/cache state,
   SysTick stopped, NVIC disabled and pending IRQs cleared, fault state handled,
   VTOR at staging, MSP from the vector, PSP/CONTROL/BASEPRI/FAULTMASK/PRIMASK
   explicitly set, and DSB/ISB barriers. Branch in assembly with a reset fallback
   for unexpected return. USB must disconnect/re-enumerate on a left trial.
6. Pause QMK split polling before a peer trial and re-establish its handshake
   after return. A right watchdog deadline is independent of UART. Coordinated
   both-half launch and native ZMK split are later work.

The [WB32 reference manual](https://www.westberrytech.com/uploads/file/WB32FQ95xx/EN_RM2905025_WB32FQ95xx_V01.pdf),
sections 7.1, 7.2.12, 7.3.16 and 18, is the hardware source to audit. Its nominal
watchdog timing is not a measured deadline. This milestone intentionally supplies
no guessed register driver or ready flag that can enable device execution.
The portable one-shot launch intent is RAM-only and consumed before validation;
it models ordering, not retention across reset or a production boot record.

## Offline deliverables and later proof

`make build-vial-trial-updater` builds a separately labeled staging receiver;
normal/updater Vial artifacts do not contain the extension. `make build-zmk-trial-fixtures`
builds separately labeled reset-return and deliberate-hang Cortex-M3 programs,
not ZMK firmware. `tools/firmware-update/trial.py` packages, validates and emits
an offline upload packet plan; it has no HID/device operations or RUN CLI.
Trial packages cannot be loaded by the ordinary updater package loader.
The dependency-free `trial.py` is included in the trial updater archive and in
each fixture directory. With Python 3.12+, `python trial.py inspect .` checks a
fixture package; `python trial.py plan . --session 42` emits its offline packet
plan to stdout. Neither command enumerates a device.

Offline tests exercise actual image/install/trial code with bounded simulated
flash, including interruption, corruption, duplicate packets, install exclusion,
commit/application/resident preservation, native unlock and transport/role checks.
They cannot execute a CPU, watchdog, USB or split recovery.

Future hardware authorization must cover, in order: measured watchdog-only reset
on each half; reset-return and hang recovery on each half; right recovery with
UART disabled; repeated trials/power loss without a loop or settings mutation;
then both returning, identity verification through left USB and automatic USB
re-enumeration with the cable stationary. Install/launch/run commands remain
separate from build/test, and none were run for this milestone.

## Local verification, 2026-10-26

`make test` passed with ASan/UBSan C fixtures and 53 repository tests.
`make check-tools` passed Ruff, strict mypy and the dedicated uv-project suites,
including 54 firmware-update tests. The independent review found a pending
BEGIN retry race; a linker-wrapper test reproduced the exact housekeeping/RPC
interleaving before the fix and passes after it. No review findings remain.

Successful builds: QMK release/debug normal and updater, Vial normal/updater,
the opt-in Vial trial updater, and both trial fixtures for left/right packages.
Application payload sizes (including package padding):

| Package | Bytes | Free in 120 KiB application slot |
| --- | ---: | ---: |
| QMK release updater | 76,972 | 45,908 |
| QMK debug updater | 81,336 | 41,544 |
| Vial updater | 92,000 | 30,880 |
| Vial trial updater | 92,988 | 29,892 |

The extension adds about 1 KiB relative to the standard Vial updater. Native
Vial build identifiers can change these payload sizes slightly between builds.
Each staging fixture is 1,024 bytes. No Zephyr/ZMK size or runtime RAM fit has
been established.

All four resident binaries have the same padded SHA256
`be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`,
matching the saved installed-updater record at
`artifacts/recovery/2026-10-19-vial-0.1.5-update.json`. Standard and trial Vial
`resident.bin` files are byte-for-byte identical. Resident sources, ordinary
image/protocol code, upstream dependencies, lockfile and stock VIA JSON have
no changes from the merged investigation baseline.

These are offline/build results only. No HID/device inspection, flash write,
keyboard reset, firmware install or trial execution was performed. Next work
is the reset trampoline and independent-watchdog implementation described above;
actual recovery proof still needs separately authorized hardware execution.

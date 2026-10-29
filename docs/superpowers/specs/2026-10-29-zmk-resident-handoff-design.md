# Application-only resident handoff experiment

The user asked to solve the update warm-restart failure while avoiding right-only
recovery. Existing installed LEFT Studio and RIGHT normal ZMK passed fresh
read-only INFO and 60-second both-half health on 2026-10-29. Residents remain
CRC32 1770733249. No new installation or reset has occurred in this investigation.

## Evidence and choice

The existing analysis in `docs/ZMK-WARM-RESTART-ANALYSIS.md` remains valid:
SYSRESETREQ does not return usable USB after installation; a later power cycle
allows the expected application to run. Software cannot read the failing PC.
The silicon supports SWD, but no connected debug adapter or verified GMK70 pad
layout was found. Watchdog retention through hardware reset remains unproven.

Alternatives considered by the implementer:

1. Add USB-only reset tracing. It cannot extract evidence while USB is absent;
   power cycling may destroy the evidence. Useful later with independent access.
2. Continue changing reset/delay/clock settings. Earlier experiments already
   failed without localizing the fault; another arbitrary patch is inappropriate.
3. Explicitly chain-load the preserved resident for installation, as a separate
   experimental path, keeping the known configured clocks and long watchdog.

The implementer's proposed experiment is (3), subject to architecture review and
offline tests before any device operation. This changes update activation only,
and must not be described as fixing ordinary SYSRESETREQ or proving the watchdog
hypothesis. User authorization to continue development and testing persists;
the resident and radio remain unchanged and the first hardware target is LEFT.

## Handoff contract

- Default production behavior remains SYSRESETREQ until the experimental path
  is explicitly selected. Staging qualification always uses its existing reset.
- Only a normal committed installation enters the resident. Do not modify its
  source, bytes, manifest ABI, protected regions or bootloader option bytes.
- The caller runs in privileged thread mode. USB must be electrically detached
  with a finite interval before disabling scheduling/IRQs. No flash write occurs
  as part of handoff preparation.
- Once interrupts are masked, stop SysTick, disable/clear all external IRQs and
  pending SysTick/PendSV, and stop bus masters that could access overwritten RAM.
  Do not reset flash, cache, analog calibration or watchdog domains.
- Refresh the already verified long watchdog once immediately before handoff.
  Preserve the known working PLL clock through resident entry; do not request
  SYSRESETREQ. The installed resident owns image validation/copy and final jump.
- A stack-free assembly tail sets VTOR, CONTROL, MSP, PSP, BASEPRI and FAULTMASK
  consistently and branches to the actual installed resident reset vector.
  No C epilogue or old-application return is possible after MSP changes.
- Validate vectors and resident identity before accepting the experimental path.
  Failure must leave the existing default reset available, never jump unchecked.
- A CRC-covered 16-byte native-startup capability footer permits handoff only
  to new compatible receivers. Old/unmarked firmware uses SYSRESETREQ.
- Experimental LEFT builds suppress peer RGB frames. The installed RIGHT
  predates the lighting transport; restart qualification must not change it
  or send a new lighting stream into its existing protocol parser.
- Native tests establish register ordering, vector bounds and caller behavior;
  linked ARM inspection establishes the no-stack tail and code placement.
  Neither establishes silicon installation timing or a successful restart.

## Hardware boundary

Freeze current package/settings evidence before an installation. The initial
upgrade still executes the old sender's reset code and may need the established
joined-LEFT USB cycle once. Do not claim that the new receiver repairs that old
sender. If a tested candidate subsequently returns automatically, verify exact
image/resident identities and 60 seconds of both-half health before another
write. An outage stops dependent writes; no blind INSTALL replay or RIGHT flash.

If architecture review cannot establish a safe application-only handoff, keep
the work offline and report the specific unresolved prerequisite. Debugger
availability is not grounds for speculative resident modifications.

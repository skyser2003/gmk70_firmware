# Right-half ZMK staging qualification

The user requested trying the right half after the successful left ZMK trial.
Continue autonomously using joined halves and USB on the left. Preserve both
resident installers, the working left application, and saved configuration.
Avoid another right-only physical recovery procedure.

Use the existing full trial updater and reset-mediated handoff. Its recovery
servicing currently accepts left retained records only; admit both valid roles
for qualified SW/IWDG returns, preserving all phase, checksum and current-reset
gates. Runtime qualification profiles remain left-only. Install the revised full
unlocked trial application on the right through left USB; never write residents.
Prove right pre-CRT watchdog recovery, then staging return and hang recovery.

Build an actual pinned `dependencies/zmk/app` right-board trial, using the right
5x9 matrix from the maintained board definition and the existing WB32 SoC/USB
integration. Keep sources and output boundaries unchanged; add `right-trial` as
an explicit supported build variant. Right trials need no cable move.

For execution proof without direct right USB, link-wrap ZMK's actual main. Only
after real main returns success and the chosen kscan device is ready, wait 500ms
in Zephyr, then issue a barrier-protected software reset. This separate opt-in
right trial never feeds IWDG. An early hang or failed initialization must reach
IWDG instead, causing the host's expected-software-reset assertion to fail.
Require the linked right image to contain the wrapper, real main and actual ZMK
symbols. Describe software-reset proof accurately; no claim of right USB typing
or native split functionality.

Alternatives: moving USB would allow the left-style descriptor proof but works
against the user's preference. UART proof needs a new bridge/transport. A small
main-return wrapper reuses the existing retained RESULT and provides a distinct
reset cause without changing its ABI or depending on the peer UART during trial.
These implementation choices are delegated under the user's prior instruction
to proceed autonomously.

Success means the exact real right image is staged, admitted, executes through
ZMK main and returns via software reset; the application/resident identities and
263 saved-configuration getters then verify. Stop physical trials on an
unexpected result, diagnose and preserve a working updater before retrying.

## Evidence-driven recovery health extension

The first right PROBE returned the correct IWDG result, but strict host observation
restarted on short relay UNAVAILABLE replies. A read-only raw timeline found
completed matching INFO/RESULT samples with isolated code7 failures lasting
15–31ms, without USB exceptions. Terminal retained results cannot distinguish
later resets, so add a runtime witness before continuing physical trials.

Full trial builds advertise capability bit64. Their existing TRACE page1 returns
four uint32 words: timer uptime_ms, successful watchdog-feed count, flags
(bit0 servicing active, bit1 recovery failed), magic/version `0x31484D47`.
Initialize count zero each boot; increment only on successful feed writes.
Full TRACE page0 remains unavailable; runtime TRACE/CAP57 remains unchanged.
This uses an operation/page already forwarded by the installed left firmware,
so no left application update is needed. The64-byte retained ABI is unchanged.

For a health-capable return, host qualification requires60seconds of matching
identity/RESULT plus active nonfailed health. Begin after uptime>2000ms; require
monotonic modular32-bit uptime and progressing feed count, with less than2s
between completed health samples. Report code7 relay retries separately and allow
them within that bound. USB exceptions, correlation failures and timeouts still
invalidate the observation window; reboot evidence fails qualification. This
measures application execution and feed writes, not hardware countdown readback.

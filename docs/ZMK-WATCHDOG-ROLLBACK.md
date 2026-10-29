# LEFT watchdog rollback and hardware-reset update result

On 2026-10-29, with the added watchdog init/feed disabled, LEFT updates returned
automatically through the **original SYSRESETREQ path** in two distinct-image tests.
The installed LEFT no longer uses the direct resident handoff for activation.
RIGHT and both resident updaters remain unchanged.

## Historical regression comparison

The Vial receiver-only checkpoint `a6a59a2` installed and returned after USB
re-enumeration; see [its validation](ZMK-TRIAL-HARDWARE-VALIDATION.md). The trial
execution extension (`fa7f351`) added early role sampling and IWDG. Its initial
GPIO access before APB clock enable was corrected; native ZMK already has the
correct APB-before-GPIO ordering. Reverting that correction would restore a bug.

The later Vial recovery addition (`d138d60`, right checkpoint `5308274`) kept
servicing an inherited watchdog. That updater still needed a joined-LEFT cycle
after installation; see [the right trial record](ZMK-RIGHT-FIRST-TRIAL.md).
ZMK adopted that register contract in `65cd79f` and starts IWDG on every permanent
boot. That is the shared addition removed for this comparison, without changing
the resident, USB/clock startup, keymap, transport or pinned dependencies.

## Actual hardware sequence

| Stage | Activation and evidence |
| --- | --- |
| Existing handoff firmware → no-watchdog A, 70,340/2513332713 | One INSTALL acknowledged. A ran with zero feeds to uptime 29,814 ms; a subsequent sample hit the startup-uptime guard, then USB became unavailable. No next INSTALL was attempted. |
| One user-confirmed joined-LEFT cold cycle | A passed 70.500 seconds of uninterrupted zero-feed health; RIGHT retained its original identity. |
| No-watchdog A → distinct B, 70,352/1589421046 | Original SYSRESETREQ; automatic return and 70.484 seconds of both-half health. |
| B → final no-watchdog Studio image, 70,340/1796245169 | Original SYSRESETREQ; automatic return and 70.516 seconds of both-half health. |

All qualified health windows had zero transport retries, relay retries and
reconnects. No further physical cycle was requested or reported for A→B or
B→final. No RIGHT application write, resident write or pad recovery occurred.
ACK is recorded separately from later identity and continuous health. Observer
discovery times are not reset-latency measurements. The failed initial bootstrap
observation took 191.688 seconds despite a nominal 165-second loop deadline:
Windows HID enumeration can block beyond the Python deadline.

The bootstrap arrived through direct resident handoff, so removing software
initialization could not stop its already-running IWDG. The observed approximately
30-second transition is consistent with expiry of the inherited long watchdog.
Only the later cold-started, uninterrupted zero-feed window qualified the sender
for the no-watchdog comparison.

The result implicates the added watchdog path. It does **not** locate the failing
CPU instruction or measure watchdog enable/configuration retention across reset.
The [earlier reset analysis](ZMK-WARM-RESTART-ANALYSIS.md) retains those limits;
its hypothetical watchdog deadline must not become a proven silicon mechanism.

## Installed checkpoint and build

Source implementation: `9686df0`, with Studio-saving support in `610946f`.

- LEFT: `wired-studio-watchdog-rollback`, USB product `GMK70 Studio No Watchdog`.
  Size 70,340, CRC32 `1796245169`, SHA256
  `b083449cc51e501368744d27d84767ea20eb3e96a7a6a899b7c08349f83613c6`.
- RIGHT unchanged: size 36,576, CRC32 `1296309606`.
- Both resident CRC32s: `1770733249`; the package still requires padded resident
  SHA256 `be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`.

Run `make build-zmk-wired-studio-watchdog-rollback` through the project environment.
This remains a separately labeled experimental, unlocked, LEFT-only Studio
package. Ordinary Studio/handoff builds retain their watchdog; the ordinary
handoff rebuilt byte-identically to the previous installed image. Do not assume
that every normal build incorporates this rollback.

Linked diagnostic/final watchdog init returns false and feed is a no-op, with
no watchdog MMIO. `gmk_zmk_update_reset` calls `sys_reboot`; no outgoing resident
handoff code survives linking. The incoming native-startup footer allows a
compatible older sender to bootstrap this image without a reset.

The explicit configuration permits local LEFT USB/wired updater writes without
watchdog readiness, rejects peer mutations, and retains the Studio worker,
journal, flash bounds, role/wired and storage-exclusion gates. Health bit 4 means
intentional watchdog removal: LEFT only, zero feeds and watchdog-ready clear.
The standard health reader accepts this policy and requires unchanged policy,
advancing uptime and zero feeds throughout observation.

## Studio and verification

Actual Studio RPC confirmed Windows/Fn/Mac/Mac Fn, each with 72 bindings. All
original four-layer bindings and names matched the fresh pre-install snapshot.
A temporary Fn layer-name edit was saved, discarded/reloaded from settings,
then restored, saved and reloaded again. The complete map matched the backup
and no unsaved changes remained. The normal local Studio app was reopened
without remote debugging, connected to COM3 and displayed all four layers.
Final native identity/health after Studio work still matched the installed
checkpoint with zero retries. No fresh human typing or visual RGB test is claimed.

Regression evidence includes native no-MMIO and local-only receiver tests,
permanent/no-watchdog Studio backend tests, repository 88 plus migration 7
tests, native ZMK fixtures and 13 Python tests, and 115 updater tests (one optional
built-resident integration skipped). Changed Python passes Ruff, formatting and
strict mypy. Affected ordinary Studio/handoff and opt-in ARM builds pass; existing
upstream RWX linker warnings remain. Independent code, linked-image and one-shot
operator reviews passed before writes. Builds/tests never operated the device.

## Limits and continuation

The installed LEFT gives up hardware-watchdog recovery from hangs. This is an
intentional rollback of the development addition, not a new general rollback
installer or a fix for watchdog-enabled firmware. RIGHT retains its old watchdog
and its update/reset behavior has not been retested here. Local LEFT RGB and
suppressed peer lighting remain as in the earlier handoff experiment.

See [sanitized validation](validation/2026-10-29-zmk-watchdog-rollback/README.md).
Private saved configuration, exact packages, operators and detailed observations
are retained under `artifacts/recovery/2026-10-29-watchdog-rollback/`. They are
provenance and recovery material; existing one-shot INSTALL markers forbid
replaying these stages. No new release asset was published.

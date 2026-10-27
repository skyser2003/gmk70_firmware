# Right-half ZMK staging qualification

On 2026-10-27, actual pinned ZMK was flashed into right staging through left USB
and executed. It reached ZMK main, confirmed the matrix device was ready, and
requested the expected software reset back to Vial. The recovered updater passed
60.672 seconds of continuous uptime/feed checks. Both residents, the left
application and all 263 saved-configuration checks were preserved.

The user requested trying right-side flashing after the successful left USB
trial. Firmware installation and trial control use USB on the left with both
halves joined. The resident installers and the left application stay unchanged.

## Software and execution proof

`make build-zmk-right-trial` builds the actual pinned `dependencies/zmk/app` for
`gmk70_right`. It uses the right 5-by-9 matrix from the maintained board definition
and the existing WB32 SoC/GPIO/USB integration. Generated files are under
`.work/zmk/right-trial/normal/`; outputs are under `artifacts/zmk/right-trial/`.
Upstream checkouts remain unmodified and builds do not download sources or
operate a keyboard.

| Item | Value |
| --- | --- |
| Maintained right port | `29ef2b4` |
| Recovery health extension | `5308274` |
| ZMK | `4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85` |
| Zephyr | `10ba6d0cb38bc3d258775d27982f707599320085` |
| Actual right image | 27,884 bytes at `0x08020000`, CRC32 `2605558610` |
| SHA256 | `20c0962875a4e3316f4a39cb30ba7a44ce9e2657267594c4cd5b9da51f8612b0` |
| Initial MSP / reset | `0x20001CE8` / `0x0802239D` |

The right trial enables an opt-in `--wrap=main` adapter. The linked Zephyr
startup calls the wrapper, which calls actual ZMK main. Only when main returns
zero and the chosen matrix device is ready does it sleep in Zephyr for 500 ms
and request a software reset. It never feeds IWDG. A hang or failure before
these gates must expire through IWDG, causing the expected-software-reset check
to fail. The image checker and native tests verify the call chain and gates.
This supplies a distinct execution witness without moving USB to the right.
It does not establish right USB typing, physical matrix event delivery or native
ZMK split operation.

## Live results

All four checks ran through left USB with both halves joined. Each completed
automatically after the earlier updater-installation power cycle described below.

| Check | Returned result | Continuous recovery observation |
| --- | --- | --- |
| [Pre-CRT watchdog](validation/2026-10-27-zmk-right/watchdog.json) | PROBE, RETURNED, IWDG + PIN (`36`) | 60.125 seconds |
| [Bare staging return](validation/2026-10-27-zmk-right/staging-return.json) | RUN, RETURNED, SOFTWARE + PIN (`40`) | 60.266 seconds |
| [Bare staging hang](validation/2026-10-27-zmk-right/staging-hang.json) | RUN, RETURNED, IWDG + PIN (`36`) | 60.469 seconds |
| [Actual right ZMK](validation/2026-10-27-zmk-right/actual-zmk.json) | RUN, RETURNED, SOFTWARE + PIN (`40`), 27,884 bytes / CRC32 `2605558610` | 60.672 seconds |

The actual ZMK run began at 19:12:35.955804 UTC and completed at
19:14:44.258153 UTC. Its final health sample had uptime 68,669 ms, 34,418
successful feed calls and active-service flags `1`. There were 529 short relay
retries across upload and recovery, with zero transport retries. Success required
the matching image/result and advancing health counters throughout the continuous
observation; retry count alone was not treated as evidence of recovery.

The [final read-only verification](validation/2026-10-27-zmk-right/final-verification.json)
at 19:14:50 UTC confirmed both application identities and resident CRCs. All
263 native getters matched the fresh backup: zero differences and zero
configuration writes. Public reports contain identities/counts, without private
keymap or macro contents.

## Recovery health and relay behavior

The first watchdog probe returned the correct retained IWDG result and matching
application identity. Its old host check nevertheless failed because each short
relay UNAVAILABLE response restarted the observation window. The
[initial report](validation/2026-10-27-zmk-right/watchdog-first-observation.json)
and [read-only diagnosis](validation/2026-10-27-zmk-right/initial-relay-diagnosis.json)
preserve that result: 12 completed matching identity/result samples, five isolated
15–31 ms UNAVAILABLE replies, and no USB exception. A terminal retained record
alone cannot reveal a later reset, so those samples were insufficient proof.

Full trial builds now advertise capability bit64 (total71). TRACE page1 returns
four little-endian words: uptime_ms, successful watchdog-feed call count, flags
(bit0 servicing active, bit1 recovery failed), and magic `0x31484D47`.
The counter starts at zero on each boot and advances only after a successful
feed call; the getter itself does no MMIO. Full TRACE page0 remains unavailable.
Runtime qualification TRACE/CAPS57 and ordinary Vial/updater builds are unchanged.
The existing left forwards this operation/page, so the left needed no update.

A health-capable trial requires 60 seconds of matching identity/RESULT and
advancing modular32-bit uptime/feed counters, active nonfailed servicing, and
less than two seconds between completed health samples. It starts after uptime
exceeds two seconds. Short code7 relay retries are counted separately and can
fit within that bound. USB/correlation interruptions invalidate the observation
window; reboot or stopped-feed evidence fails qualification. These counters
witness software execution/feed calls, not hardware countdown readback.

## Updater installation and preservation

The revised right updater was transferred through left USB. Its warm restart did
not verify automatically; the user reconnected left USB once with both halves
joined. That restored the right with the complete expected application. No pad
bridge, direct right USB or right-only flashing was required. The
[installation record](validation/2026-10-27-zmk-right/application-installation.json)
records that limitation; its underlying warm-restart cause remains unmeasured.
All 263 saved-configuration checks passed afterward without restoration writes.

| Region | Verified application/identity after installation |
| --- | --- |
| Left | Unchanged trial updater, 95,772 bytes, CRC32 `44883505` |
| Right | Trial updater with health witness, 95,908 bytes, CRC32 `2635584202` |
| Right SHA256 | `9de53c3eec05f6bdcd0b55fb91f257390c0eaf500818a7bb202554c7e99b6131` |
| Both residents | CRC32 `1770733249`, unchanged |

The experimental unlocked updater variants use the user's explicit
`VIAL_INSECURE` development exception. Trial builds remain separate from normal
release assets. Physical device operations remain separate from build/test
commands.

## Software verification and current scope

Validation passed `make test`, `make test-zmk`, `make check-tools`, secure and
unlocked full Vial trial builds, and real left/right ZMK builds. Native tests
cover both-role recovery gates, rejected/corrupt records, the main-return adapter
and health observation failures. Independent review checked the linked startup
call chain, retained memory, old-left forwarding compatibility and recovery gates.
The left ZMK binary stayed byte-identical.

Both halves currently run the experimental unlocked Vial trial updaters shown
above. The right ZMK image remains in staging; it is not the ordinary application
and does not launch automatically at power-on. The left and right have now
executed separate bounded ZMK trials. Physical input, simultaneous ZMK split
operation, wired transport, radio and permanent ZMK operation remain future work.
The warm updater-installation restart limitation above is still open.

# GMK70 basic ZMK parity validation

This change implements the [audited basic features](../../ZMK-BASIC-PARITY.md)
using pinned native ZMK/Zephyr code and the maintained GMK70 hardware integration.
The work is on `codex/zmk-basic-parity`, based on signed `e5e8a8dd`.

Both halves are now installed and independently verified on the same
105,876-byte application, CRC32 2614232591. Each received exactly one acknowledged
INSTALL through LEFT USB, with automatic return and no manual power cycle,
right-only connection or recovery pads. Both resident CRCs remain 1770733249.
All four saved Studio layers and 296 bindings are unchanged; runtime metadata
and geometry pass after each update. See [sanitized hardware evidence](hardware-result.json).

The implementation has not yet been qualified for physical sleep current, wake
timing, host remote wake or overnight battery life.

The resident, lower/upper retained RAM ranges, staging and settings-bank addresses
are unchanged. Both roles use the identical canonical application. Permanent
images contain neither a hardware watchdog nor outgoing resident handoff.

## Verification coverage

Dedicated tests execute the pinned native HID/indicator, keymap, RGB, USB control,
activity, power-management and formatting implementations. Board tests cover the
stock radio mouse packets and sleep commands, roles and queued releases, atomic
power negotiation, GPIO/EXTI ownership, native reset/factory magic, and physical
LED/DMA quiescence. Settings tests cover full keymaps, legacy pages, compaction,
interrupted writes/reset, migration during load, native ID persistence and Studio
readiness notification. Python tooling runs Ruff and strict mypy.

A dedicated fixture also executes unmodified native `behavior.c`: a missing
historical behavior's ID stays reserved, existing IDs stay stable, and native
commit allocates new IDs above that high-water mark. The wrapper seeds/restores
the native handler's RAM table without synthetic flash records. Direct reads and
unrelated subtrees are unchanged; malformed/exhausted IDs are rejected.

Independent review reproduced and resolved four power defects before completion:

- Stale configured/suspended USB state after physical unplug blocked native sleep.
- A held key plus pending RGB settings prevented critical shutdown.
- Joined halves with expired peer health could omit the peer-wake pulse.
- A manual soft-off request rejected by active USB could remain queued.

Physical USB power now overrides stale native connection state. Qualified critical
saves use the same runtime allowlist and storage lease while bypassing the held-key
idle requirement. Peer wake follows physical split routing. Blocked manual off
returns an error before queuing any request.

Stack allocations remain unchanged. Compiled call-chain inspection is a useful
bound, but it does not replace hardware stack watermarks or physical endurance
qualification. Private logs and any device snapshots stay under ignored `.work/`.

## Final results

`make test-zmk`, `make test` and `scripts/check-tools.sh` pass on the final tree.
Edited C passes `clang-format --dry-run --Werror`; edited shell scripts pass
`bash -n`; whitespace and credential-pattern checks pass. Dependency pins,
submodule sources and the stock VIA definition are unchanged.

All 19 affected supported distributions compile: five QMK variants
(release/debug/default and both updaters), six Vial variants
(normal/updater/unlocked/diagnostic and both trials), and these eight ZMK variants:

| ZMK variant | Application bytes | RAM bytes | Unallocated RAM bytes |
|---|---:|---:|---:|
| Wired updater | 51,952 | 17,700 | 10,844 |
| Wired peripheral | 51,960 | 17,708 | 10,836 |
| Wired update test | 51,976 | 17,724 | 10,820 |
| Wired qualification | 51,812 | 17,732 | 10,812 |
| Studio canonical | 105,876 | 28,162 | 382 |
| Studio migration bridge | 106,200 | 28,170 | 374 |
| Studio update test | 105,892 | 28,178 | 366 |
| Studio radio qualification | 106,364 | 28,186 | 358 |

Studio applications are bounded by 106,496 bytes; canonical flash headroom is
620 bytes. All ten canonical stack allocations sum to the unchanged 14,336 bytes.
The admitted RAM interval remains `0x20000040..0x20006fc0`. Inspected compiled
paths include startup mount/CRC at 728/1,024 bytes, service settings save/remount
at 1,268/2,048, and low-priority RGB at 688/768, each including a conservative
64-byte exception/kernel allowance. These are inspected call-chain budgets,
not measured stack watermarks or an exhaustive stack proof.

Canonical application SHA-256:
`dd1cb2d7ea6659d6e76a560864b3d3fa8334bc550524125ba10b7e530d763310`.
The unchanged resident padded to its 8,192-byte reservation has CRC32 1770733249
and SHA-256
`be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`.

Native Zephyr size/timing options keep the full feature set within these bounds.
At 1 kHz, existing 1 ms worker sleeps can take about 2 ms because of relative
timeout alignment. Hardware UART/SPI/USB timing retains cycle-based waits;
native uptime remains 64-bit. Hardware latency and endurance are unmeasured.

The source remains uncommitted: the required GPG signing attempt failed with
`No pinentry`. No unsigned commit or push was used as a fallback. The reviewed
uncommitted source patch/overlay, immutable per-target packages, clean Studio
backup and operation evidence were frozen privately before installation.

## Charging update correction

The stock QMK board sends charger controls `0x64`/`0x65`/`0x66` every second.
The earlier ZMK port only read the GPIOs and queried percentage; it never told
the radio module that charging was active or complete. The user observed both
the LED meter and Windows Bluetooth remaining at 3% until power cycling.

The correction forwards the real GPIO state through the existing radio queue,
refreshes it every second, and retains the three-second percentage queries.
Native-core and combined-UART tests cover charging/full/unplug sequences,
incremental 3-to-67% cache/sensor propagation without restart, immutable partial
frames, ACK ownership, one bounded nonfatal charger retry, typing preservation,
wrap-safe timing and sleep admission. Independent review found no material issue.
The compiled battery poll calls the protected charger-state adapter; its added
wrapper uses eight stack bytes and the core update uses none.

All four affected Studio ARM profiles were rebuilt; the full ZMK/repository
suites and Ruff/strict-mypy tooling checks pass again. The other fifteen build
profiles have no affected production sources. The correction adds eight static
RAM bytes and does not change the resident or stack allocations. No installation
was performed during that offline correction; the installation below followed
the user's explicit flash request.

## Installation and live checks

The clean pre-installation backup contains four layers and 296 bindings. Baseline
and post-update both-half windows passed for LEFT (70.187/70.250 seconds) and
RIGHT (70.234/70.265 seconds), each with zero transport retries, relay retries,
reconnects or watchdog feeds. Both INSTALL acknowledgments validated on the first
request. LEFT continued running across the RIGHT update. Studio checks before
installation, after LEFT and after RIGHT preserve the exact saved keymap,
including layer names/order, and validate native behavior metadata and geometry.

Live charging telemetry is fresh: 83/83 queries/replies after LEFT and 267/267
after RIGHT, cable connected, full-status false, zero radio errors. The reported
percentage stayed at 60% across these observations; an increase was **not**
observed. These checks prove continued replies and a healthy command path, not
physical state-of-charge accuracy or live Windows Bluetooth refresh. Physical
key/knob/LED actuation after this update and battery-only STOP/wake/endurance
remain unmeasured. Private evidence stays under
`.work/zmk-basic-parity/device-install/`.

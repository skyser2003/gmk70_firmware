# ZMK to QMK Board Safeguards Plan

**Goal:** Bring repository-added ZMK correctness safeguards into the shared VIA/Vial board implementation after deleting the integrated old Codex branches.

**Scope:** Preserve press-time function/media mappings, track battery holds by position, validate/expire battery samples, recover malformed radio input safely, release outgoing host reports, and revalidate/expire deferred updates. Preserve native framework protocols, storage, unlock, residents, definitions and dependency pins. No keyboard operations or history rewriting.

**Architecture:** VIA/Vial continue sharing `qmk-common`. Use small pure-C helpers shared with ZMK where the state/algorithm is already the same (battery held-position masks). Adapt framework hooks locally; the user explicitly permits separate QMK/ZMK implementations when sharing requires excessive conversion or abstraction. Keep the installed ZMK behavior and permanent no-watchdog policy.

## Tasks

- [x] Delete the four old remote Codex branches and integrated local branch, using checked leases.
- [x] Function flip: preserve the press-time output across flip/OS changes and duplicate presses; use dedicated native regressions before production edits. Keep mapping tables and persistent flag semantics.
- [x] Radio: port bounded raw-tail quarantine, partial-frame expiry and range/freshness checks; add `bool md_battery_is_fresh(void)` with a 10-second age bound while retaining existing getters. Release outgoing reports before actual host/channel changes; preserve same-channel wake behavior.
- [x] Updater: expire abandoned sessions despite read-only/rejected traffic; recheck local USB/role/native authorization before deferred mutations while preserving inherited peer authorization and trial separation.
- [x] Battery: share pure held-position masks with ZMK, preserve simultaneous holds and clear original actions on physical release/live remapping. Render stale/invalid percentage as unknown and keep invalid telemetry out of low-battery decisions.
- [x] Integrate build/test source lists and document intentional differences from reconstructed stock behavior.
- [x] Format edited C/Python, run the complete relevant test/lint/type suites, compile all affected QMK/Vial variants and affected canonical ZMK images, then obtain independent review.

## Coordination

Function-flip work owns its new helper/tests and only the flip block in `gmk70.c`.
Radio work owns `module.c/.h`, wireless/transport implementation and their tests.
Updater work owns `update/qmk_update.c` and updater adapter tests.
The primary implementer owns battery/held-position helpers, hardware hooks,
remaining `gmk70.c` battery integration, shared build rules, test script and docs.
Do not concurrently edit another task's sections or commit intermediate work.

## Verification focus

- Press/release pairing after flip or OS selection changes, including ordinary passthrough keys.
- Multiple physical battery bindings, live replacement, mode changes and sleep ownership.
- Oversized/truncated raw tails containing valid-looking control notifications.
- Host changes while keyboard/media input is held, with finite ordered releases.
- Upload timeout with frequent INFO/rejected mutations; USB/suspend/unlock changes between admission and execution; right relay without local USB.
- Byte/CRC-identical residents, unchanged vendor JSON and clean pinned dependencies.

## Completed verification

- `make test`, `make test-zmk` and `make check-tools` passed. Edited C also passed
  clang-format checks; the build-hook Python regression passed Ruff, strict mypy
  and its targeted test after the final integration assertion was added.
- All five QMK configurations built: release, debug, default and both updaters.
  All seven Vial configurations built: normal, secure updater, unlocked updater,
  radio diagnostic, both early trial updaters and runtime watchdog qualification.
- ZMK Studio updater, Studio update test, radio qualification and wired
  peripheral built. Canonical Studio still allocates 27,576 of 28,544 RAM bytes,
  leaving 968 bytes; retained regions remain excluded. Extracting the shared
  helper adds 40 application bytes without changing allocated RAM.
- ARM disassembly for both frameworks confirms native 6KRO and NKRO dispatch
  branches resolve to the board's report-copy wrappers. Tests cover native weak
  modifier overlap in both release orders, consumed events, modifier-only cache
  suppression, rejected presses and releases while disconnected/suspended.
- UART regressions cover every subscribed data-loss flag, including software
  queue overflow, while excluding WB32's synthetic noise flag. The queue-loss
  regression failed before the mask correction and passed afterward.
- Independent review found no remaining actionable issues after modifier
  ownership, rejected-release cleanup and UART queue-overflow corrections.
- Every resident remains byte-identical. Padded SHA256 is
  `be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`,
  CRC32 `1770733249`. Dependency pins, upstream files, the original VIA definition
  and native settings schemas remain unchanged.
- No keyboard operations, settings writes, application flashing or resident
  rewrites were performed. Device backups remain under ignored local storage.

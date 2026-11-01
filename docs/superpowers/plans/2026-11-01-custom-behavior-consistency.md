# Custom Behavior Consistency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Close the four confirmed custom behavior gaps and verify a fixed cross-stack behavior checklist.

**Architecture:** Shared board adapters for VIA/Vial, board-owned ZMK adapters with unchanged pinned native implementations, native-path regressions, one integration review.

**Tech Stack:** C, pinned QMK/Vial/ZMK/Zephyr, native sanitizer fixtures, existing uv tool projects.

**Spec:** docs/superpowers/specs/2026-11-01-custom-behavior-consistency-design.md

## Global Constraints

- Preserve existing uncommitted main changes; all tasks use the original main checkout, not the historical attached worktree.
- Keep dependencies, resident bytes, settings encodings/partitions, behavior IDs and original VIA definition unchanged.
- Permanent ZMK never initializes or feeds a watchdog; no device operations, settings reset, commit or push.
- Source scripts/project.sh in Bash, use pinned offline tools, format edited code, run strict native warnings, full tests and affected builds.
- Use dedicated tests and real native boundaries; no copied maintained implementations, macro-generated product logic, new threads, smaller stacks or RAM-bound changes.

## Review Focus

- Ordinary and remapped keys released across disconnect/host/mode transitions must not survive as stale native report state.
- Reload/reset and slow storage work must not race a pending physical pairing deadline.
- RGB changes while wireless or USB-unconfigured must remain pending without blocking the system work queue, then save the latest value when eligible.
- Held custom shortcut modifiers must coexist with physical/native implicit modifiers and overlapping shortcuts without clobbering or leakage.
- Keymap, native RGB settings and existing startup behavior-ID writes must remain compatible; linked images must fit RAM with retained records excluded.

### Task 1: Shared VIA/Vial radio lifecycle

**Files:** firmware/qmk-common/drivers/wireless/module.c/.h, wireless.c, transport.c as necessary; keyboards/zuoya/gmk70/gmk70.c, hardware.c, function_flip.c/.h and wls/wls.c only as needed; tests/qmk/ radio/report fixtures and a dedicated lifecycle regression.

**Interfaces:** Preserve existing public/native report APIs. Any connection-transition notification must be shared and invoked outside parser/IRQ ownership where native report cleanup is unsafe. Parent integrates scripts/test.sh.

- [x] Add a failing report-level regression using actual board gates/native report state: A down, radio disconnect, A up, reconnect, B down must not produce A+B. Cover both pinned frameworks and 6KRO/NKRO.
- [x] Add cases for pairing/reject states, modifiers/remapped outputs, held versus released inputs, repeated status, host switches, and all physical switch states including both pins low.
- [x] Implement minimal shared recovery and physical-mode admission corrections, preserving same-channel wake behavior and native modifier ownership.
- [x] Format and run focused native/sanitizer tests; save RED/GREEN evidence and self-review report.

### Task 2: ZMK native remapping cancellation

**Files:** firmware/zmk/radio/radio.c, radio/CMakeLists.txt; tests/zmk/radio_kernel_test.c, run_radio_kernel_tests.sh and focused native keymap-path fixtures/stubs as needed.

**Interfaces:** Keep gmk_radio_hold/unhold/select and the existing radio selector API. Add linker wrappers only for native map-replacement paths that need cancellation. Parent owns top-level test/build orchestration.

- [x] Reproduce a hold surviving actual native Discard/Reset paths before changing firmware.
- [x] Cover before-deadline, queued work, already-pending pair, failed/no-op reload, individual edits and independent physical releases.
- [x] Cancel before native binding replacement/reset can block or race the pairing timer; include other actual binding-replacement paths found in the same native subsystem.
- [x] Format, run focused regressions and record RED/GREEN plus self-review evidence.

### Task 3: ZMK RGB runtime persistence

**Files:** firmware/zmk/lighting/rgb_settings.c, lighting/CMakeLists.txt; studio persistence helpers if practical; update/service.c for an existing-worker poll if needed; tests/zmk/rgb_settings_test.c, run_lighting_tests.sh and focused save/backend fixtures.

**Interfaces:** Preserve native RGB bytes and all existing bool-setting APIs. Runtime persistence uses gmk_zmk_settings_save_runtime with configured-USB recheck. Native startup behavior-ID writes keep their existing backend path. No new thread or duplicate RGB schema.

- [x] Reproduce the native save callback losing a wireless/failed save and bypassing the configured-USB gate.
- [x] Exercise latest-value coalescing, eligibility after more than the 60-second native debounce, USB loss after queueing, busy/error retries, reboot/load format, and no system-work deadlock.
- [x] Implement bounded deferred retry using existing work/storage machinery, sharing utility code where practical and keeping the native saved representation unchanged.
- [x] Keep flip/NKRO tests passing if their helper is reused; format, strict-check and report RED/GREEN evidence and RAM/state additions.

### Task 4: ZMK held flip modifier ownership

**Files:** firmware/zmk/function_flip/core.c/.h, flip.c, CMakeLists.txt as necessary; tests/zmk/function_flip_* and native HID/listener integration fixtures.

**Interfaces:** Preserve gmk_flip_toggle/poll and bool-setting APIs. Separate custom flip modifier contributions from native global implicit modifiers without changing unrelated native binding semantics.

- [x] Promote the ignored host reproduction into dedicated maintained tests with unchanged pinned hid.c and hid_listener.c; first assert GUI remains while holding Mac 3 and pressing/releasing A, and while holding 3+4 then releasing either.
- [x] Cover real GUI/other modifiers, native implicit shortcuts, consumer/brightness mappings, duplicate presses, mode/flip changes, native clear/host transitions and NKRO/boot projection.
- [x] Implement bounded independent modifier ownership through the existing adapter/report boundary; avoid suppressing native errors or duplicating the pinned HID implementation.
- [x] Format, run focused suites with sanitizers and both modifier-release-report configurations; record RED/GREEN evidence and self-review.

### Task 5: Fixed checklist and integration verification

**Files:** docs/CUSTOM-BEHAVIOR-CHECKLIST.md; docs/CUSTOM-FEATURE-PARITY.md and relevant feature docs; scripts/test.sh/Makefile only to wire dedicated regressions.

- [x] Record one acceptance matrix for every existing repository-added behavior family, with native-path evidence, explicit intentional limits and hardware-only checks.
- [x] Run task-scoped independent reviews, resolve material issues, then one broad interaction review against the fixed checklist.
- [x] Run make test, make test-zmk, make check-tools, formatting/lint/typing and all affected QMK/Vial/ZMK distributions. Preserve exact resident hashes and linker RAM exclusions.
- [x] Verify all pre-existing local source changes remain and no private device data enters maintained files. Report implemented results and actual limits without claiming exhaustive hardware parity.

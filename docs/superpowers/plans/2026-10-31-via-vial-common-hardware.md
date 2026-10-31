# VIA/Vial Common Hardware Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task by task.

**Goal:** Give VIA and Vial the same existing GMK70 hardware hooks through one maintained implementation.

**Architecture:** Move the Vial RGB-off battery task, LED power reconciliation and early physical-key activity hook into `firmware/qmk-common/keyboards/zuoya/gmk70/hardware.c`. Enable that source and its task wrappers through the shared build rules. Keep platform protocol, storage, native unlock, keycode aliases and fork-specific RGB index routing in their current adapters.

**Tech Stack:** C, the independently pinned QMK/Vial frameworks, GNU Make, native sanitizer tests and strictly typed Python build tests.

**Scope:** This implements the user's request to synchronize VIA/Vial and share hardware code. It does not import ZMK changes or operate the keyboard. The common-code approach follows the requested repository layout and avoids a second maintained implementation.

## Constraints

- Preserve the vendor VIA JSON, custom keycode values, native storage schemas and unlock policy.
- Keep sleep/suspend ownership of LED power and restore darkness after temporary indicators.
- Count physical key activity once before framework features consume/replay events.
- Build directly from maintained sources with clean pinned dependencies.

## Review Focus

- RGB-off queries on either half, cable removal and charging/full transitions.
- GUI or split synchronization enabling RGB after hardware power was disabled.
- Suspend/presleep/STOP/wakeup retaining control of LED power.
- Physical events consumed by user hooks and synthetic events with unbalanced releases.
- VIA/Vial protocol and storage adapters staying isolated in the real build graph.

## Task 1: Share existing hardware behavior

- [x] Add a failing build-graph regression requiring both frameworks to enable the same hardware hooks.
- [x] Move the existing hardware implementation into the common module and remove the Vial copies and late VIA activity counting.
- [x] Move hardware tests out of the Vial protocol fixture; run the common RGB and physical-activity fixtures against both pinned frameworks.
- [x] Verify the build-graph and native behavior regressions.

## Task 2: Validate supported distributions

- [x] Update the VIA/Vial documentation to describe the shared behavior.
- [x] Format changed C/Python and run `make test check-tools`.
- [x] Build QMK default/release/debug and release/debug updaters; build Vial normal, native-unlock updater, unlocked updater, diagnostic updater and trial updater variants affected by the common module.
- [x] Review the diff, source/definition/dependency invariants and final test/build results.

## Verification result

The build-graph regression failed for both frameworks before the move and passes
afterward. Native sanitizer fixtures cover both pinned frameworks, both LED
halves, temporary overlays, power restoration, sleep ownership and physical versus
synthetic events. The final `make test` passes 89 repository and 22 migration tests
plus the native suites. `make check-tools` passes formatting, Ruff, strict mypy,
89 repository, 22 migration, 16 diagnostics, 187 updater and 32 ZMK Python tests;
one optional built-resident integration is skipped.

All twelve ARM distributions build: five QMK profiles, six normal/experimental
Vial profiles and the separate runtime-watchdog qualification profile. Linked
symbols and source-origin records confirm the shared hardware implementation.
The reviewed resident checksum, vendor VIA definition, dependency pins and ZMK
sources are unchanged. Independent review found no actionable introduced bugs.
No connected-device operation or hardware validation was performed.

No commit, publication or device installation is part of this request.

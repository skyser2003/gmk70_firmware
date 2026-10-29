# ZMK watchdog rollback implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline; obtain one independent review before live writes.

**Goal:** Test whether removal of the shared trial-watchdog addition restores LEFT hardware-reset updates.

**Architecture:** Labeled LEFT-only Studio A/B variants disable watchdog MMIO,
report their exception honestly, accept only local wired updates, and use
SYSRESETREQ. Bootstrap through the existing marked-receiver handoff.

**Tech stack:** Maintained WB32 C, pinned ZMK/Zephyr, native sanitizer tests,
existing Python build/package tooling and separate one-shot device operators.

**Spec:** `docs/superpowers/specs/2026-10-29-zmk-watchdog-rollback-design.md`

## Global constraints

- Existing development branch, one maintained repository, pinned dependencies.
- RIGHT 36,576/1296309606 and both residents 1770733249 unchanged.
- Preserve 16 KiB Studio settings and existing base bindings.
- No device operation in build/test; one explicit INSTALL per stage.
- No watchdog keys or other watchdog MMIO in diagnostic init/feed.

## Review focus

- Diagnostic bypass must never allow peer writes or ordinary-build bypass.
- Incoming capability footer does not imply outgoing handoff is present.
- No-feed health must never masquerade as watchdog readiness.
- Inherited running watchdog must expire before the actual no-watchdog test.
- ACK and later manual recovery must not become automatic-restart claims.

## Task 1: Offline diagnostic

- [x] Add failing native tests for no-MMIO init/feed and local-only write gates.
- [x] Observe failure against current implementation.
- [x] Add opt-in Kconfig, receiver-only footer, labeled A/B configs and targets.
- [x] Keep settings writes gated; add explicit bit-4 diagnostic health.
- [x] Update ELF/package allowlists and tests for the exact variants.
- [x] Run focused tests, repository/ZMK/updater suites and Python checks.
- [x] Build diagnostic A/B and rebuild affected ordinary Studio/handoff variants.
- [x] Review code, linked reset/watchdog bodies and the one-shot operator.

## Task 2: Separate hardware experiment

- [x] Snapshot actual saved Studio configuration and current identities.
- [x] Freeze diagnostic A/B, working recovery image, operator and hashes.
- [x] One LEFT bootstrap INSTALL; observe inherited watchdog expiry/recovery.
- [x] Require 70 seconds of stable zero-feed diagnostic health.
- [x] One A-to-B INSTALL using SYSRESETREQ; bounded return observation.
- [x] Install final no-watchdog image with Studio saving restored; verify both identities/health/Studio.
- [x] Record exact results, limitations and current hardware state; commit work.

## Progress

Preflight: historical sources reviewed; hardware matched the frozen handoff
checkpoint. Ruling: continue autonomously on the existing development branch
under the user's explicit rollback/test request.

Offline diagnostic: repository 88 plus migration 7 tests, native ZMK fixtures
and 13 Python tests, and 113 updater tests (one optional skip) passed. Changed
Python Ruff/format/strict mypy passed. Independent code/artifact review passed.
Operator review corrected stale-report and boot-continuity guards before writes.

Bootstrap: one LEFT INSTALL acknowledged; initial zero-feed runtime reached
29,814 ms before the startup-uptime guard failed and USB became unavailable.
No next write occurred. User confirmed one joined-LEFT cold cycle; A then passed
70.500 seconds of uninterrupted zero-feed health.

Hardware comparison: A→B SYSRESETREQ returned automatically and passed 70.484
seconds of zero-feed health. Ruling: keep the requested watchdog removal installed
and restore Studio journal saving, instead of reintroducing the watchdog via the
old recovery image. Ordinary builds and RIGHT remain unchanged.

Final implementation: source `610946f`; settings/health regressions observed RED
then GREEN, independent code/linked-image/operator reviews passed. Final suite:
repository 88 plus migration 7 tests, native ZMK fixtures and 13 Python tests,
and 115 updater tests (one optional skip). Changed Python checks and affected
ARM builds pass; ordinary handoff remains byte-identical.

Final hardware: one B→final INSTALL; original SYSRESETREQ returned automatically
and passed 70.516 seconds of both-half zero-feed health. Actual Studio save/reload
of a temporary Fn name passed, all original names and 288 bindings were restored,
and no unsaved changes remain. Normal Studio reopened without remote debugging,
COM3 connected and four layers visible. Final identity/health after UI work
confirmed LEFT 70,340/1796245169, RIGHT unchanged and both residents unchanged,
with zero retries. No further cycle after bootstrap cold start.

See `docs/ZMK-WATCHDOG-ROLLBACK.md` and sanitized validation. Private evidence is
frozen separately and must not be published or replayed.

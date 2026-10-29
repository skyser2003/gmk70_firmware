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
- [ ] Install final no-watchdog image with Studio saving restored; verify both identities/health/Studio.
- [ ] Record exact results, limitations and current hardware state; commit work.

## Progress

Historical sources reviewed; current hardware still matches the frozen handoff
checkpoint. Ruling: continue autonomously under the user's explicit rollback/test
request; no repeated design approval or new checkout is needed.

Offline results: repository88 + migration7 tests, ZMK native fixtures/13 Python tests, updater113 tests (one optional integration skipped) passed. Changed Python Ruff/format/strict mypy pass. Linked diagnostic init/feed are no-op and reset calls sys_reboot; ordinary handoff rebuild is byte-identical to installed A. Independent firmware review found no blocker. Independent device-operator review found and corrected stale-report/boot-continuity guards before writes; final review passed. One LEFT bootstrap INSTALL was acknowledged. Initial zero-feed runtime lasted about30seconds, then startup-uptime guard failed and USB became unavailable. No second INSTALL was sent; user confirmed a single joined-LEFT cold cycle and fresh70-second qualification is in progress.

Ruling after hardware comparison: A→B SYSRESETREQ returned automatically and passed70.484seconds of zero-feed health. Keep the user-requested watchdog removal installed instead of reintroducing it via the old recovery image. Extend the labeled opt-in with Studio journal saving; ordinary builds and RIGHT remain unchanged. New settings/health tests observed RED then GREEN; independent code/artifact review passed. One final B→functional no-watchdog image update remains.

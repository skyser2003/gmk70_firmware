# Universal GMK70 ZMK Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline and
> superpowers:test-driven-development; one fresh final review.

**Goal:** One full-featured, role-aware application and package for both halves.

**Architecture:** Reuse PB9-selected wiring and peripheral services, explicitly
exclude RIGHT from Studio storage, and qualify one selected install target per
frozen case with shared non-replaying INSTALL dispatch.

**Tech Stack:** Pinned ZMK/Zephyr C, native sanitizer fixtures, strict Python/uv.

**Spec:** [design](../specs/2026-10-30-zmk-universal-design.md).

## Global Constraints

- Existing branch/in-place inline execution follows prior autonomous preference.
- No resident, watchdog policy, stacks, dependencies or personal keymap changes.
- Load `scripts/project.sh`; strict typing, formatting, lint and dedicated tests.
- Build/test never operate hardware; separate qualified joined-LEFT operations.
- Keep private snapshots/evidence outside Git; sign commits and push.

## Review Focus

- RIGHT startup must finish even with unreadable/unknown settings-bank contents.
- LEFT storage failures must still lock editing rather than fall back to defaults.
- A legacy Studio package must never gain RIGHT admission by accident.
- After SEAL, the selected target is sealed and its peer must remain idle/unchanged.
- INSTALL ACK loss or rejection must cause neither INSTALL replay nor ABORT.

### Task 1: Runtime role and explicit universal packaging

**Files:** `firmware/zmk/studio/settings_backend.c`, Studio config/help,
`firmware/zmk/tools/check_image.py`, `tools/firmware-update/zmk_package.py`,
`update.py`, `scripts/build_zmk.sh`; dedicated existing backend/image/package tests.

**Interfaces:** Existing backend readiness remains false on RIGHT;
`settings_backend_init()` must bypass flash there. Validated image reports add
`runtime_roles: ["left", "right"]` for role-aware Studio. New profile
`zmk-studio-universal` uses the Studio application bound and pinned resident.

- [x] Add/run RED native RIGHT no-read/no-write/no-queue/startup tests and image/
      package admission regressions; preserve existing LEFT cases.
- [x] Implement minimal role guards and explicit validated profile; run GREEN.
- [x] Format C/Python, compile canonical image and inspect RAM/linked role checks.

### Task 2: Qualified target selection and one-shot install

**Files:** `tools/firmware-update/{update.py,qualified_install.py}` and dedicated
`tests/test_{update,qualified_install,zmk_package}.py` under that project.

**Interfaces:** `Updater.request_install(target: int, session: int) -> Reply`
dispatches once and polls only STATUS. `QualifiedInstall.create_case(...,
target: int = 0)` freezes one target; CLI `create-case --target {left,right}`.
Legacy cases default to LEFT. Both-half health/identity proof stays mandatory.

- [x] Add/run RED tests for both selected targets, wrong/no-op target, legacy
      rejection, sealed context and missing/lost/malformed ACK without replay/ABORT.
- [x] Implement shared one-shot dispatch and target-aware freeze/install/observe;
      run GREEN, full suites, formatter/lint/strict mypy and affected ARM builds.
- [x] Fresh whole-change review; fix important findings with RED→GREEN, then sign
      source after private-data review.

### Task 3: Joined-LEFT hardware validation and delivery

**Files:** New universal guide/validation, latest handoff/build documentation.

**Interfaces:** New universal package installed separately on LEFT then RIGHT;
each case admits exact current size/CRC pairs and unchanged resident identities.

- [ ] Fresh clean Studio backup and both-half baseline; freeze/install LEFT once,
      observe automatic return/health and exact full-map preservation.
- [ ] Freeze the same application for RIGHT, baseline/install once through LEFT,
      independently verify both equal images/residents and saved map.
- [ ] Normal Studio and final health; private archive/checksums; sanitized docs,
      sign/push and report actual hardware coverage/limits.

# Runtime Watchdog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox syntax for tracking.

**Goal:** Prove watchdog recovery and its bounded fallback on the left while preserving the recovered right half.

**Architecture:** A separate runtime qualification profile reuses the trial receiver and retained record with normal Vial startup. Portable deadline/record logic, isolated packaging and host capability checks support a small runtime adapter.

**Tech Stack:** C, Cortex-M3/WB32FQ95, pinned Vial/QMK, Python/uv, shell.

**Spec:** `docs/superpowers/specs/2026-10-27-runtime-watchdog-design.md`

## Global Constraints

- Sole maintained GMK70 repository; upstream dependencies remain unmodified.
- Resident, native settings schema, flash layout and wired/role gates remain unchanged.
- Build and test commands never operate the keyboard.
- Runtime profile has separate build caches and artifact names; RUN is unavailable.
- No right-half installation, driver installation or resident writes in this milestone.
- Load `scripts/project.sh` before runtime/build commands.

## Review Focus

- DWT enable/readback failure must reject the probe without hanging.
- Counter wrap or stall must still reach bounded software reset.
- Corrupted or stale retained records must never authorize runtime RUN.
- A software fallback must never be reported as hardware watchdog success.
- Runtime profile must retain normal startup and exclude early GPIO/handoff.

### Task 1: Portable deadline and runtime record entry

**Files:** new `update/trial_deadline.{c,h}`, existing `update/trial_boot.{c,h}`, corresponding `tests/update/` tests.

**Interfaces:** `gmk_trial_deadline_t`; `bool gmk_trial_deadline_begin(gmk_trial_deadline_t *, const gmk_trial_mmio_t *, uint32_t clock_hz)`; `void gmk_trial_deadline_rebase(gmk_trial_deadline_t *, const gmk_trial_mmio_t *)`; `bool gmk_trial_deadline_expired(gmk_trial_deadline_t *, const gmk_trial_mmio_t *)`; `bool gmk_trial_boot_runtime_enter(volatile gmk_trial_boot_record_t *, uint32_t role)`.

- [x] Add failing tests for 12-second wrap-safe deadline, counter stall, absent/failed enable, invalid clocks, and runtime PROBE-only retained entry.
- [x] Run tests and observe missing implementation failures.
- [x] Implement portable helpers; preserve existing RUN admission and record size.
- [x] Run sanitized portable tests and format edited C.

### Task 2: Isolated build profile

**Files:** `scripts/build_vial.sh`, `tools/build-support/project_build.py`, `check_trial_elf.py`, root `Makefile`, repository tests.

**Interfaces:** `GMK70_TRIAL_RUNTIME=yes` selects `runtime-watchdog` profile only for trial modes; define `GMK70_TRIAL_RUNTIME`; separate cache/artifact/archive suffix `-runtime-watchdog`; ELF checker accepts `--runtime-watchdog`.

- [x] Add failing profile isolation/invalid selection tests.
- [x] Observe failures, implement explicit profile routing and labels.
- [x] ELF check requires retained RAM/transport wrappers and absence of early boot, GPIO role and handoff symbols for runtime profile. Use explicit exceptions, not Python asserts, for safety checks.
- [x] Run repository tests and Python formatter/mypy.

### Task 3: Host capability and fallback proof

**Files:** `tools/firmware-update/trial_run.py`, its dedicated tests.

**Interfaces:** CAPS=0x13 echoes nonce, four result words at16; bits/timeout from spec. FALLBACK_TEST=0x14. CLI `fallback --target left` expects software reset and result action2. Probe requires PROBE capability; RUN requires RUN capability before uploading.

- [x] Add failing capability, unsupported RUN, fallback and wrong-reset-result tests.
- [x] Observe failures, implement capability checks and fallback CLI.
- [x] Run uv formatter, strict mypy and dedicated tests.

### Task 4: Runtime firmware integration

**Files:** new `update/trial_runtime.c`, `post_rules.mk`, `trial.{c,h}`, `trial_platform.h`, `qmk_update.c`, Vial adapter, adapter tests and `scripts/test.sh`.

**Interfaces:** Same platform prepare/reset/result APIs; runtime init after CRT; fallback test sets an explicit runtime-only skip-watchdog flag. CAPS centrally reports profile. Runtime RUN always unavailable.

- [x] Add failing protocol/adapter tests for authorization, capability echo and disabled RUN.
- [x] Observe failures; integrate helpers and runtime adapter without early startup code.
- [x] Run `make test`, `make check-tools`; build supported affected QMK/Vial variants and runtime profile; inspect ELF boundaries.
- [x] Obtain independent review and fix material findings before hardware writes.

### Task 5: Left-only hardware proof and evidence

**Outcome:** Software fallback proven. Watchdog recovery failed; left restored to
the published release, right untouched. Independent watchdog qualification and
staging execution remain outstanding; see `docs/ZMK-TRIAL-EXECUTION.md`.

**Files:** separate ignored device operation scripts; `docs/ZMK-TRIAL-EXECUTION.md`; sanitized recovery evidence.

- [x] Read both live INFO/resident identities and take fresh native settings backup.
- [x] Install left application only; verify resident and right application unchanged; restore/verify fresh settings.
- [x] Run left watchdog then fallback proofs, accepting only matching actual reset flags.
- [x] Verify native settings and both halves again; record exact results and limitations; commit reviewable source/evidence.

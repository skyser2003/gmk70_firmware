# Right ZMK Trial Implementation Plan

> Use superpowers:subagent-driven-development. Keep physical actions separate from build/test commands.

**Goal:** Flash and verify an actual right ZMK staging trial through left USB.
**Architecture:** Qualify both-role Vial watchdog servicing; then prove genuine right ZMK main execution through an opt-in software reset wrapper.
**Spec:** docs/superpowers/specs/2026-10-27-zmk-right-trial-design.md

## Constraints and review

- Preserve both residents and left application; native settings unchanged.
- No dependency downloads in builds, no upstream edits, no driver installation.
- Role extension must preserve corruption, phase and current-reset gates.
- Wrapper executes real main before reset; errors/hangs remain IWDG failures.
- The right ZMK board uses its actual 5x9 matrix, not the left matrix.

### Task 1: Both-role recovery servicing

Files: trial_boot.c/h, tests/qmk/trial_boot_test.c, tests/qmk/trial_platform_test.c.
- [x] Write/run failing both-role tests, retaining all invalid-record/reset exclusions.
- [x] Extend qualified servicing to both valid roles; runtime profile unchanged.
- [x] Format, run make test, build full unlocked and secure trial updater; review.
- [x] Add reviewed runtime health witness and bounded relay-aware observation after the initial strict observation failed; retain transport/reset/feed failure gates.

### Task 2: Actual right ZMK trial

Files: firmware/zmk/boards/zuoya/gmk70_right, trial/main_return.c, Kconfig/CMake;
scripts/build_zmk.sh, check_image.py, Makefile, dedicated native/repository tests.
- [x] Add failing native main wrapper tests and right build/image validation tests.
- [x] Add right board and opt-in post-main reset proof; build right-trial explicitly.
- [x] Verify actual ELF main/wrapper, vector/RAM/flash contract and dependency pins.
- [x] Run ZMK/native/Python checks and review.

### Task 3: Right hardware proof

- [x] Fresh configuration backup and installed identities; freeze reviewed package.
- [x] Install right full trial application only, verify left and residents unchanged.
- [x] Qualify right watchdog, return and hang recovery via left USB.
- [x] Flash actual right ZMK package and verify expected SW reset, matching image, stable return.
- [x] Verify both applications, residents and saved configuration; save public evidence.
- [x] Commit/push result and explain scope and current keyboard state.

Outcome and limitations are in `docs/ZMK-RIGHT-FIRST-TRIAL.md`. The right updater
upgrade needed one joined-left USB reconnect after its warm restart timed out;
the underlying cause is unmeasured. All subsequent trial checks recovered
automatically with 60-second continuity evidence. No resident or left application
write, pad bridge or direct right USB flashing occurred. Both halves currently
run Vial; native ZMK split/input/permanent operation remains future work.

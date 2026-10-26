# Staging trial launcher implementation plan

**Goal:** Implement the first software milestone from `docs/ZMK-LOCAL-HANDOFF.md`, with offline proof of staging/install separation and an explicitly disabled hardware launch path.

**Architecture:** A Vial-only opt-in trial updater shares the existing transfer buffer and split RPC, using packet namespace `0x71` instead of install namespace `0x70`. The resident, ordinary image validator, install protocol and upstream dependencies stay unchanged. No device command is run.

**Spec:** `docs/ZMK-UPDATER-TRIAL.md`; concrete recovery contract in `docs/ZMK-TRIAL-LAUNCHER.md`.

## Constraints and review focus

- Resident binary, ABI 1, application and native settings must be preserved.
- Trial vectors target `0x08020000`, maximum `0x1E000` bytes; trial stack stays below `0x20006FC0`, boot magic remains `0x20006FFC`.
- Trial BEGIN requires an erased ordinary commit page, never erases it, and only DATA writes staging.
- Ordinary/trial sessions are exclusive, including queued requests, retries, abort and timeout.
- Native physical unlock, wired USB left-master/right-slave checks apply to trial requests too.
- RUN remains UNAVAILABLE in firmware until independent watchdog and reset/startup integration have hardware evidence. Offline simulation is not that evidence.
- Build/test/package tools never enumerate or write a device.

## Tasks

- [x] 1. Add portable trial validator/protocol and consumed RAM launch intent with simulated-flash tests: malformed vectors, transfer interruption/corruption, commit exclusion, application/resident preservation, ordinary INSTALL rejection and one-shot consumption.
- [x] 2. Integrate opt-in Vial trial-updater mode with shared buffer/session exclusion and native authorization. Exercise actual adapter with simulated USB/RPC, locked/wireless/role failures, abort and expiry. Keep RUN disabled and prove normal builds exclude the extension.
- [x] 3. Add offline trial package validator/packet plan plus separately labeled return/reset and hang fixtures under firmware/zmk. Test manifest identity, target, memory contract, hashes, ordinary install rejection; no HID support in this milestone.
- [x] 4. Format edited C/Python, run make test and make check-tools; build QMK release/debug normal/updater, Vial normal/updater/trial-updater and fixtures. Compare resident bytes to the pre-change baseline. Record sizes, limits and the exact later hardware proof.

Implementation is performed in this checkout on `codex/zmk-trial-launcher`, preserving the user's sole maintained project directory. Continue under the user's explicit implementation authorization; hardware execution is separately excluded.


## Execution record

All four tasks completed for this offline milestone. The recovery design selects
an early reset trampoline; hardware implementation and proof remain explicit
follow-on work, not an enabled launch path. Independent review's Important
pending-ownership race was reproduced by a maintained linker-wrapper regression,
fixed by serving pending requests solely from queued/cached data, and re-reviewed
without further findings. The minor offline-tool packaging gap was also fixed.
Verification evidence, sizes and the next hardware gate are recorded in the spec.

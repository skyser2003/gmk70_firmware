# GMK70 USB Studio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement task by task. Prior autonomous execution preference applies; no repeated plan-approval prompt.

**Goal:** Connect the GMK70 central to ordinary local ZMK Studio, persist edited keymaps and retain firmware updating.

**Architecture:** Standard pinned Studio CDC-ACM plus an updater Feature collection on HID0. A bounded custom settings backend journals whole WB32 pages in two app-tail banks, serviced by the existing update worker.

**Tech Stack:** Pinned ZMK/Zephyr, nanopb/Studio messages, native C, Python/hidapi, CMake and existing ARM tooling.

**Spec:** `docs/superpowers/specs/2026-10-28-zmk-studio-design.md`.

## Global Constraints

- Sole maintained repository/current task checkout; all new platform sources under `firmware/zmk/`.
- Upstream checkouts unmodified, exact dependency pins, explicit setup, offline builds.
- Resident CRC `1770733249` unchanged; retain SRAM `20000040..20006fbf`, staging/commit and external Vial settings.
- Studio APP max `0x1a000`; settings `0801c000..0801ffff`; common APP ABI remains `0x1e000`.
- USB Feature report ID4/page FF60/usage62/payload32; normal usage61 framing unchanged.
- New experimental unlocked `wired-studio-updater`, `profile=zmk-studio`, LEFT install only; no RIGHT flash.
- Source root environment with `scripts/project.sh`; builds/tests never operate devices.

## Review Focus

- A stale Feature response or reconnect must not acknowledge a new mutation; test invalidation and both framing transitions.
- An interrupted erase/program must preserve unrelated acknowledged settings; test arbitrary target-page damage at every write boundary.
- Startup behavior-ID saves must not deadlock or be mistaken for host writes; test worker startup and storage-fault rejection.
- A concurrent settings save and updater BEGIN/INSTALL must not race reset/flash; test shared admission and failed work without watchdog feed.
- Studio update size bounds and USB endpoint totals must follow actual linked artifacts; inspect ELF/descriptor output and exact boundary tests.

---

### Task 1: Pinned Studio inputs and generated physical metadata

**Files:** Modify `dependencies/sources.lock.json`, `firmware/zmk/PROVENANCE.md`, `firmware/zmk/tools/pyproject.toml`/`uv.lock`, `scripts/build_zmk.sh`; create `firmware/zmk/tools/studio_layout.py`, `tests/zmk/test_studio_layout.py`, Studio board overlay/config and `board.cmake`/pre-DT hook as required.

**Interfaces:** `studio_layout.generate(keyboard: Path, via: Path) -> str` emits a physical-layout DTS fragment in maintained matrix order. Build variant `wired-studio-updater` selects it, a bounded code region, standard CDC and Studio settings policy; existing variants retain current metadata/configuration.

- [x] Write layout tests with hand-checked first row, left Fn `(4,2)` and right space geometry; reject missing/duplicate matrix positions, malformed KLE and non-centi geometry. Observe RED before implementation.
- [x] Implement generator and configure-time dependencies before DTS preprocessing; generate only under `.work/zmk/wired-studio-updater/updater/`.
- [x] Add exact nanopb `7307ce399b81ddcb3c3a5dc862c52d4754328d38` and Studio-messages `6cb4c283e76209d59c45fbcb218800cd19e9339d` pins from upstream manifests. Lock needed protobuf compiler/runtime tools; explicitly run `make setup-zmk`.
- [x] Run generator/workspace Python format, strict mypy and dedicated tests via its uv project; verify clean exact dependencies. Expected: PASS; no device operation.
- [x] Commit this deliverable and record test command/results.

### Task 2: Durable page settings and flash ownership

**Files:** Create `firmware/zmk/studio/settings_store.[ch]`, `settings_backend.c`, worker integration header/CMake/Kconfig and native tests under `tests/zmk/`; modify native `update/wb32_flash.c`, `update/service.c`, `update/receiver.[ch]` only as needed for restricted page adapter/admission.

**Interfaces:** Store core exposes initialize/load/save/delete via explicit flash view/page-write callbacks; journal pages256, two8192-byte banks at the exact spec addresses. `gmk_zmk_settings_poll(env)` executes queued synchronous settings jobs on the update worker. `gmk_zmk_receiver_storage_idle()` reports no pending local mutation or active upload/commit. `gmk_zmk_settings_ready()`/health status gates unsafe Studio mutation after failed initialization. A settings page adapter writes only the reservation; existing `gmk_flash_page` guards remain.

- [ ] Write native RED tests for blank mount, persistent key/value/delete/no-op, malformed storage and exact region bounds using the real format implementation and an array-backed page fault injector.
- [ ] Implement bounded flash-offset index, packed snapshots, one-record journal pages and commit-last bank switching. Validate all on-flash lengths/version/generation/CRCs; reject capacity/oversize before mutation.
- [ ] Extend tests to every interrupted erase/program in append and compaction, stale generations/torn tails, retries after uncertain writes, overwrite/delete and unchanged unrelated data. Expected: only old/new single-setting state, no resurrected deletion, no silent format.
- [ ] Add native Zephyr settings adapter and worker request/completion, trusted startup writes, updater admission exclusion and storage-fault handling. Test load APIs, initialization ordering, queue saturation, concurrent BEGIN/INSTALL and no watchdog feeding through unfinished work.
- [ ] Run new sanitizer/native suites, existing update/receiver suites, formatting and `make test`. Expected: PASS. Commit and ledger evidence.

### Task 3: Studio USB, host framing and complete build

**Files:** Create Studio Feature adapter under `firmware/zmk/update/`, modify `raw_hid.c`, CMake/Kconfig, `tools/firmware-update/update.py`, `trial_run.py`, their dedicated tests; modify native image checker/packager/build targets for Studio profile. Add actual descriptor and lifecycle native tests.

**Interfaces:** Existing `gmk_zmk_raw_hid_init/poll` names remain, with a Studio branch binding HID0 and using EP0 Feature frames. A single `usb_hid_register_device` wrapper appends report4 vendor collection and preserves upstream callbacks. `UsbLink` shared `_send_packet`/`_read_packet` helpers select legacy usage61 or Feature usage62 without probing and are reused by trial/health correlation. `zmk-studio` packages have LEFT-only install admission and max104KiB.

- [ ] Write RED native tests for one registration, actual descriptor prefix/feature suffix, invalid report rejection, single request admission, old-response invalidation and EP0 snapshot stability while the worker publishes.
- [ ] Implement Feature callback/worker adapter without interrupt writes or keyboard semaphore changes; preserve all original keyboard callbacks.
- [ ] Write host RED tests for both usages/framings, ambiguity, wrong IDs/short frames, not-ready polling, stale/session/nonce responses and legacy→Studio reconnect; implement shared helpers and retain specialized trial correlation.
- [ ] Test receiver/package maximum exactly `0x1a000` and rejection above it before BEGIN touches commit; enforce LEFT profile target. Existing profile limits remain unchanged.
- [ ] Build actual Studio and affected normal/qualification ZMK variants. Verify Studio symbols, native physical layout, CDC/Feature descriptor endpoint total, wrapper interception, Flash/RAM, pinned resident and old trial hashes. Diagnose real compiler/configuration failures before changing assumptions.
- [ ] Run C format, `make test-zmk`, `make test`, both affected Python uv format/Ruff/strict mypy/dedicated suites. Expected: PASS. Commit and ledger actual results.

### Task 4: Independent review and LEFT-only hardware qualification

**Files:** New status guide/validation evidence, current README/handoff, private standalone operator reports only; no device operation in build/test/setup.

**Interfaces:** Use maintained firmware updater with explicit LEFT target; use standard Studio serial protocol with pinned messages for readback and reversible save/reload. Preserve a frozen current A recovery package and exact residents before writing.

- [ ] Dispatch one fresh whole-feature code review against this task's base, spec/plan and review-focus cases; settle findings and verify fixes before hardware.
- [ ] Independently validate actual package hashes/build facts, current LEFT/RIGHT identities and health through existing USB, then freeze reviewed image/operator inputs.
- [ ] Install only LEFT Studio. Wait for verified return or explicit restart timeout, inspect actual USB presence, and use joined-left reconnection only if necessary. Never replay INSTALL to fix a timeout.
- [ ] Verify keyboard/Feature updater and standard CDC serial, actual Studio device/layout/keymap RPC, both-half health and unchanged residents. Test a reversible keymap save, reload and compatible LEFT self-update retention; restore original mapping and reverify. Keep RIGHT image unchanged.
- [ ] Record actual typing/UI confirmation when available; keep unsupported/unmeasured cases explicit. Update usage instructions, commit/push reviewed results, preserve evidence privately and finish.

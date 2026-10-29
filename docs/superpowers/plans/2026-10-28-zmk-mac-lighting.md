# GMK70 Mac layers and lighting implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Build four-layer, lighting-capable native ZMK candidates and document restart evidence without disturbing the installed keyboard.

**Architecture:** Retain upstream keymap/RGB behavior logic and the installed settings reservation. Extend the bounded settings format and add a board-owned LED strip driver and wired frame transport.

**Tech Stack:** Pinned ZMK/Zephyr, WB32 Cortex-M3 C, devicetree, native C fault-injection tests, Python unittest.

**Spec:** `docs/superpowers/specs/2026-10-28-zmk-mac-lighting-design.md`

## Global Constraints

- No device writes, resets or disconnects; preserve resident, dependency pins and immutable VIA definition.
- Existing Studio settings stay at 0x0801c000..0x0801ffff; two 8192-byte banks.
- Windows layer IDs 0/1, Mac IDs 2/3, 72 keys each.
- Keep build/test commands independent from device control.

## Review Focus

- Version-1 bank migration under interrupted compaction must preserve acknowledged edits.
- A fully edited four-layer map with 12-byte bindings must fit alongside behavior IDs and names.
- Existing two-element layer ordering must not hide new Mac layers.
- Incomplete, stale, reordered or corrupted lighting chunks must never display a partial frame.
- USB/wired interrupts and flash work must not corrupt LED timings or block watchdog progress.

### Task 1: Four layers and compatible bounded settings

**Files:** settings_store.[ch], gmk70_wired.keymap, studio.overlay/conf, settings_store_test.c, test_studio_uart.py, new board-keymap regression.

**Interfaces:** Preserve public settings functions and flash bounds; new pages version 2 accept compact canonical binding names; legacy version 1 remains readable.

- [x] Add failing capacity/legacy migration tests covering 288 bindings, metadata, remount and page cuts.
- [x] Replace RAM entry metadata with record offsets, encode binding names, retain legacy decoding and commit-last compaction.
- [x] Add four stock Windows/Mac layers and mode switches with semantic mapping checks.
- [x] Tune matched CDC/RPC rings, inspect compiled RPC stack usage, and build Studio within RAM bounds.
- [x] Run native settings/framing/keymap tests and commit the deliverable.

### Task 2: Native lighting driver and both-half transport

**Files:** new firmware/zmk/lighting and driver bindings/config, wired_protocol.[ch], wired_wb32.c, kscan integration, board config/keymap, tests/zmk native lighting tests, Makefile.

**Interfaces:** Virtual 77-pixel strip; local 36/41 pixel hardware; transaction 11 retains exactly 16 input bytes. Worker-side render only; updater ABI unchanged.

- [x] Add failing native tests for RGB encoding, finite peripheral timeout, transaction CRC, full-frame commit and loss/reordering/reconnect.
- [x] Implement the WB32 hardware adapter from pinned vendor sources with explicit provenance and bounded progress.
- [x] Implement queued split-frame transport and scanner-side output outside the wire lock.
- [x] Enable native ZMK RGB behaviors and stock Fn lighting keys on both OS function layers.
- [x] Run affected native tests and actual Studio/normal/qualification builds; commit.

### Task 3: Analysis, documentation and complete verification

**Files:** docs/ZMK-WARM-RESTART-ANALYSIS.md, docs/ZMK-MAC-LIGHTING.md, handoff/readme/build-info and relevant regression checks.

**Interfaces:** Evidence report has no device side effects; hardware validation and installation remain explicitly outstanding.

- [x] Compare actual reset/resident/clock paths with vendor and upstream evidence, distinguish facts from hypotheses.
- [x] Document OS detection support, default-layer limits, Studio lighting controls and all Fn bindings.
- [x] Format edited sources; run make test, make test-zmk, tooling checks and affected ARM builds.
- [x] Obtain one fresh whole-feature review, address Important/Critical issues with regression tests, archive ledger and commit final evidence.

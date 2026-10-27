# Permanent Wired ZMK Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Implement tasks sequentially with native tests and review before device work.

**Goal:** Install genuine permanent wired ZMK on both halves while retaining left-USB updates and the unchanged residents.
**Architecture:** One role-sensing ZMK application, native WB32 flash/UART adapters, existing portable updater protocol, Vial-compatible split wire framing, and raw HID updater USB alongside keyboard HID.
**Tech Stack:** Pinned ZMK/Zephyr, C, Python uv projects, ARM GCC14.2, native sanitizer tests.
**Spec:** docs/superpowers/specs/2026-10-27-zmk-permanent-design.md

## Global Constraints

- Preserve residents byte-for-byte, native Vial EEPROM and stock VIA JSON.
- Application base `0x08002000`, maximum `0x1E000`; staging `0x08020000`, commit page `0x0803FF00`, page size256. SRAM below `0x20007000`, preserve QMK boot magic.
- Reuse portable common image/protocol sources directly without modification; no upstream edits or build downloads.
- Build/test/setup commands never operate hardware; hardware writes are separately controlled and stop on failed verification.
- Wired USB first; one identical role-sensing image on both halves, no published release replacement.

## Review Focus

- A staging-qualified image must never overwrite its own executing code.
- Right migration compatibility must work with the exact installed Vial transaction table and UART format.
- Inherited watchdog state must not cause a post-install reboot loop.
- USB/serial concurrency, stale replies and resets must not install an unverified image or lose updater access.
- Peripheral disconnect must release remote held keys; native saved configuration must remain untouched.

### Task 1: Native flash and permanent watchdog adapters

**Files:** Create `firmware/zmk/update/wb32_flash.c`, `wb32_watchdog.c/.h`, native test seams and `tests/zmk/run_update_platform_tests.sh`; update provenance for vendor-derived code.
**Interfaces:** Export common `gmk_flash_view`, `gmk_flash_page`, `gmk_running_size` signatures from `image.h`; `bool gmk_zmk_watchdog_init(void)` and `void gmk_zmk_watchdog_feed(void)` for permanent builds. Integrator supplies a linked image-end symbol; adapter size includes file padding to four bytes.

- [ ] Add/run failing native tests: resident/app/unaligned/out-of-range writes rejected; allowed256-byte staging and commit pages; failed operations/readback mismatch; IRQ/FHSI/PCLK state restored.
- [ ] Add/run failing watchdog tests for inherited running watchdog, finite readiness failures and correct long-period configuration/feed ordering. Staging builds must not refresh it.
- [ ] Implement native adapters from pinned vendor sequences without changing common/QMK/resident code. No protection or option-byte changes.
- [ ] Format and run native sanitizer tests; report tests and source provenance. Commit task files and review.

### Task 2: Wired matrix and Vial-compatible transport

**Files:** Create `firmware/zmk/wired/` C/header sources, native tests under `tests/zmk/`, and kscan integration/binding under `firmware/zmk/drivers/kscan/`, `firmware/zmk/dts/bindings/`.
**Interfaces:** `bool gmk_wired_is_left(void)`, `void gmk_wired_set_update_handler(void (*)(const uint8_t[32], uint8_t[32]))`, `bool gmk_wired_update(const uint8_t[32], uint8_t[32])`, `bool gmk_wired_remote_matrix(uint16_t rows[5])`. Native UART/kscan module starts automatically at Zephyr init; polling and RPC share a mutex. Handler must queue work and return promptly, never flash in serial processing.

- [ ] Derive exact19-entry installed Vial transaction profile from pinned source and full trial ELF; save reproducible compatibility evidence. Matrix payload is five little-endian uint16 rows; updater RPC ID18, RPC framing IDs13..16.
- [ ] Write/run failing native tests for handshake XOR19, CRC8 from actual upstream implementation, RPC validation/bounds, partial frames/timeouts, wrong ID/lengths, stale output, and matrix disconnect releases.
- [ ] Implement native WB32 UART1 A9/A10 1152008N1 full duplex with finite transactions and correct clock/pin/FIFO setup. Preserve vendor attribution.
- [ ] Implement 5ms-debounced role-sensitive local matrix and combined10x9 kscan delivery, sharing existing maintained pin/key facts. Right serves matrix and updater RPC; left polls and forwards. No EEPROM/radio writes.
- [ ] Run native tests and compile checks; commit task files and review. Root CMake/Kconfig/board hookup is Task3.

### Task 3: Actual permanent ZMK receiver, board, build and host packages

**Files:** Create `firmware/zmk/update/` receiver/rawHID integration, `boards/zuoya/gmk70_wired/`, build/package/image checking support, dedicated test cases. Modify module CMake/Kconfig, `scripts/build_zmk.sh`, Makefile, host `tools/firmware-update/update.py` and its tests.
**Interfaces:** Consume Task1/2 exports. `gmk_wired_set_update_handler` registers a nonblocking protocol receive/cache function; a dedicated worker executes common protocol state machine and 300ms delayed install reset. Reuse normal updater namespace0x70/ABI1. Host `profile=zmk` accepts only validated application-slot images and the exact resident hash, with retained debug/Vial support.

- [ ] Add/run failing tests for duplicate/busy/correlated requests, invalid/mutating USB role/mode gates, staging write exclusion, flash failure, reset after committed ACK and housekeeping timeout.
- [ ] Add keyboard+32-byte raw HID USB, VID342D PID E491 manufacturerHS usageFF60/61; reuse host discovery. Include read-only runtime uptime/boot continuity evidence without changing ABI1 INFO.
- [ ] Add universal board, actual10x9 keymap and function layer, handedness, wired switch detection, permanent watchdog, application-slot linking and bounded staging-qualification variant. Staging variant permits no running-code write or durable install.
- [ ] Add explicit `make build-zmk-wired-updater` and staging qualification targets with separate caches/artifacts and clear labels. Existing trial builds remain unchanged. Package reviewed resident bytes from pinned QMK/Vial output with exact identity validation; builds never flash.
- [ ] Add `zmk` host package profile and migration/rollback tests. Validate actual ELF vectors/load segments, combined matrix, receiver/native adapter symbols and no main-return wrapper in permanent target.
- [ ] Run make test/test-zmk/check-tools and affected supported builds; review implementation and whole branch before hardware writes.

### Task 4: Staging proof, permanent installation and update round trips

**Files:** Separate ignored device scripts/reports; tracked `docs/ZMK-PERMANENT-WIRED.md`, validation evidence and updated handoff/status.

- [ ] Freeze reviewed packages and resident identities; fresh backup and read-only running identities. Prove bounded left staging USB/receiver and right Vial transport; do not overwrite executing staging code.
- [ ] Install permanent left only; verify both INFO identities, stable uptime and unchanged residents before dependent writes. Retain right Vial while proving left staging flash/self-update or rollback.
- [ ] Install permanent right through left; verify both genuine ZMK applications, wired matrix/USB behavior and no reboot loop. Preserve resident/configuration evidence and physical limitations honestly.
- [ ] Prove right update and left self-update through left USB, final identities/uptime and resident preservation. Diagnose failures without blind retries or right-only recovery requests.
- [ ] Commit/push reviewed evidence and report current firmware, working features, deferred features and any unresolved limitation.

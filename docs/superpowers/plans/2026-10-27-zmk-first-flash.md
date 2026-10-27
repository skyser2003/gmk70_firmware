# ZMK First Flash Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans. Continue through hardware proof; do not stop at an intermediate milestone.

**Goal:** Build, flash and verify an actual left-side ZMK USB trial with resident and right preserved.

**Architecture:** Diagnose watchdog setup with retained checkpoints and probe-scoped fault recovery. Build a pinned out-of-tree WB32 Zephyr/ZMK port in parallel, then execute it through the proven staging launcher.

**Tech Stack:** C/Thumb, WB32FQ95, pinned Vial, ZMK/Zephyr, CMake/West, Python/uv.

**Spec:** `docs/superpowers/specs/2026-10-27-zmk-first-flash.md`

## Global Constraints

- Sole GMK70 repository; maintained ZMK integration under firmware/zmk.
- Resident and right application unchanged; left-only hardware qualification first.
- Build/test commands perform no device operations or downloads.
- Sources pinned and unmodified; setup explicit; preserve licenses.
- Fresh settings backups and resident/application checks around application writes.
- No driver installation.

## Review Focus

- Fault recovery must not change ordinary startup/fault handling.
- Diagnostic record must not admit staging RUN or corrupt the64-byte RAM contract.
- USB setup/data/status transitions must respect endpoint/FIFO limits.
- ZMK links/execution must fit staging120KiB and reserved RAM.
- A flashed fixture or plain Zephyr sample must never be labeled actual ZMK.

### Task1: Retained diagnostic core

**Files:** update/trial_boot.{c,h}, tests/qmk/trial_boot_test.c.
**Interfaces:** bool gmk_trial_boot_trace_enable(record); bool gmk_trial_boot_trace_io(record,address,value,bool write); bool gmk_trial_boot_trace_fault(record,kind,cfsr,pc); bool gmk_trial_boot_trace_report(record,uint32_t words[5]). Record pointer is volatile gmk_trial_boot_record_t*, report pointer const volatile.
- [x] Write/run failing tests for ABI2 emptyPROBE-only entry, CRC, completion and admission exclusion.
- [x] Implement distinct-access checkpoint and fault snapshot helpers, preserving64bytes.
- [x] Run sanitized tests and format C.

### Task2: Fault and watchdog diagnosis

**Files:** trial_runtime.c, new trial_fault.S, protocol/adapter/host tests.
**Interfaces:** TRACE0x15 readonly two pages, FAULT_TEST0x16 left/nativeunlock only; trace flags writebit8/faultkindlow8.
- [x] Add failing tests for active-only fault capture, checkpoint ordering, protocol gates.
- [x] Implement probe-scoped exception capture/reset, inspect actual ELF vectors.
- [x] Prove software fallback and deliberate fault recovery on left; use captured IWDG evidence to qualify recovery servicing. Exact watchdog persistence across reset remains unmeasured.
- [x] Qualify left no-feed watchdog and staging return/hang recovery.

### Task3: Pinned ZMK dependency and SoC/board integration

**Files:** dependencies/sources.lock.json additions, firmware/zmk module/SoC/board, scripts/setup_zmk.sh, scripts/build_zmk.sh, Makefile targets, build boundary tests.
**Interfaces:** dependencies/zmk, dependencies/zephyr and modules pinned; module root firmware/zmk; board gmk70_left; source ZMK/app; build artifacts under artifacts/zmk/left-trial. Legacy USB driver symbol CONFIG_USB_DC_WB32. Hardware register header firmware/zmk/soc/arm/westberry_wb32/wb32fq95xx/soc.h if that matches pinned Zephyr layout; choose modern SoC root as needed and communicate exact path.
- [x] Explicitly setup exact dependencies; write build-boundary tests before routing.
- [x] Implement clock/GPIO/board/keymap and staging linker/RAM bounds.
- [x] Compile actual ZMK and inspect source provenance, symbols, vectors, size.

### Task4: WB32 USB device-controller driver

**Files:** firmware/zmk/drivers/usb/device/usb_dc_wb32.c, local Kconfig/CMake and focused controller tests.
**Interfaces:** pinned Zephyr legacy usb_dc API; CONFIG_USB_DC_WB32; wb32_usb_clock_enable() in SoC integration provides clocks/reset/pins before driver attach; USB IRQ value from pinned vendor table. Coordinate SoC declarations with Task3.
- [x] Read vendor USB reference and pinned API; test endpoint/control state transitions with fake MMIO.
- [x] Implement EP0 and interrupt HID endpoints, reset/address/stall handling, ISR/callback integration.
- [x] Compile against actual pinned headers; verify USB clock and electrical attachment in hardware only after launcher recovery proof.

### Task5: Actual ZMK flash and proof

- [x] Run required software checks and independent review before hardware writes.
- [x] Back up settings; flash real ZMK trial into left staging through unchanged resident/Vial.
- [x] Verify ZMK USB identity/execution and watchdog return; correct failures and repeat as needed.
- [x] Record exact binaries, live evidence, current keyboard state; commit/push maintained sources.

Completed 2026-10-27. Exact live execution, USB identity, stable recovery and
preservation evidence: [first left trial](../../ZMK-LEFT-FIRST-TRIAL.md).

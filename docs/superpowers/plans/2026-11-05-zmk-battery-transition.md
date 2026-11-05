# ZMK Battery Transition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent transient full notifications and expose enough transition evidence to test the USB-entry 100% fault.

**Architecture:** Qualify the physical full input in the existing UART worker. Publish a small immutable trace through an additional read-only battery operation without changing the established getter. Use native execution in the existing authorized development checkout.

**Tech Stack:** C11/Zephyr, strict Python, existing uv environments and sanitizer-backed native fixtures.

**Spec:** `docs/superpowers/specs/2026-11-05-zmk-battery-transition-design.md`

## Global Constraints

- Keep wired percentage reporting, exclusive USB/radio typing, one universal file, saved settings, the resident updater and the permanent no-watchdog policy.
- Normal application flash is limited to 106,496 bytes.
- Check actual RAM against 28,544 bytes, preserving the existing 14,336 stack bytes.
- Do not remove product features to fit the diagnostic.
- Builds/tests perform no device writes.
- User has authorized this implementation; continue without another planning approval round.
- Preserve pre-existing uncommitted work and signed-commit preferences; do not commit unrelated changes or create unsigned commits.

## Review Focus

- A raw full pulse must not turn into a transmitted full frame during a USB quiet interval.
- A genuine full input at startup or near uptime wrap qualifies after exactly 1,000 ms.
- A pin change during a partial UART frame cannot rewrite that frame.
- Cache-only diagnostic reads must not select hosts, issue radio requests or extend update leases.
- Saturated long-running counters and unavailable percentage history decode without misleading freshness claims.

## Task 1: Qualify full and collect worker-owned transition metadata

**Files:** `firmware/zmk/battery/battery.c`, `battery.h`, `tests/zmk/battery_radio_uart_test.c`, `battery_test.c`.

**Interfaces:** Preserve `gmk_battery_get(struct gmk_battery_status *)` and its existing fields; use a separate `gmk_battery_trace_get(struct gmk_battery_trace *)`. Qualification is worker-owned and updates the existing full state passed to the radio.

- [x] Add failing native tests for a short full pulse, exact 1,000 ms qualification, immediate falling edges, startup, wrap and partial-frame history.
- [x] Run battery/UART fixtures and observe the intended failure.
- [x] Implement the qualifier, saturated trace counts and completed-frame recording.
- [x] Update old full-input fixtures for the qualified semantics and run the focused tests to green.

## Task 2: Expose and decode a compatible read-only trace

**Files:** `firmware/zmk/update/battery_status.c`, `tests/zmk/battery_status_test.c`, `tools/firmware-update/read_battery.py`, `tests/test_read_battery.py` under that tool.

**Interfaces:** Additional operation `0x23` with magic `BTRC`; operation `0x21` stays byte-for-byte compatible. `BatteryReader.trace(nonce: int) -> BatteryTraceSnapshot`; CLI `--trace` issues one trace getter.

Resource ruling: the temporary bridge and F24 qualification variants retain
their old feature sets and the qualifier but reject the new trace operation.
The canonical and update-test builds collect and expose it. Encoding/receiver
tests cover supported and unavailable configurations. The trace has a separate
cache getter and encoder so ordinary battery reads retain the original layout.

- [x] Add failing packet-layout, malformed nonce/flags/payload and typed reader tests, including raw full preceding qualification.
- [x] Observe the failures, implement encoding/decoding and run those fixtures to green.
- [x] Verify that the receiver applies its existing LEFT USB/wired gates and that getter tests prove zero MMIO/UART side effects.

## Task 3: Build, review and qualify the candidate

**Files:** Validation report under `docs/validation/2026-11-05-zmk-battery-transition/`; update handoff and battery documentation.

- [x] Format edited C/Python, run strict typing/lint and relevant static analysis.
- [x] Run `make test` and `make check-tools` through `scripts/project.sh`.
- [x] Build affected ZMK variants; record flash/RAM/stack limits and exact candidate identity.
- [x] Obtain a fresh review of the task-specific changes and address material findings with tests.
- [x] Inspect LEFT USB availability; if available, preserve saved configuration and qualify the first LEFT candidate using the established separated device procedure.
- [x] Record actual hardware results and limitations. Do not claim the USB-entry issue fixed based on offline tests alone.

Trial outcome: LEFT installation, settings and both-half health checks pass.
The percentage jump remains: cable insertion on BT2 produces 86% -> 100% with
zero full signals/commands. Unplugged Bluetooth subsequently updates to 98%
without a power cycle. The candidate is not a complete battery-estimate fix;
the converted voltage and stock radio estimator require further investigation.
RIGHT, residents, radio firmware/RAM and pairing remain unchanged. See validation.

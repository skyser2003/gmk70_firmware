# GMK70 Wireless Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for integration and
> superpowers:test-driven-development for each behavior change. Focused parallel
> USB-memory and migration tasks are independent of radio firmware integration.

**Goal:** Implement Bluetooth/profile and proprietary dongle typing through the
stock GMK70 radio, restore eight wireless bindings and report the remaining five.

**Architecture:** One bounded UART3 owner serves battery and radio frames in the
existing worker. Three local linker wrappers route keyboard/consumer reports
and clears while native USB remains available to Studio and recovery.

**Tech Stack:** pinned ZMK/Zephyr, WB32 C, native C/C++ fixtures, strictly typed
Python/uv device and migration tools, typed Windows host verification.

**Spec:** [design](../specs/2026-10-29-zmk-wireless-design.md).

## Global Constraints

- Preserve residents, saved Studio settings, radio firmware and bonds; no CLEAN.
- No permanent watchdog, new thread, smaller stack or imported-source mutation.
- Preserve physical USB/wired, USB-ready, role and service write gates.
- Build/test/conversion stay separate from explicit device operations.
- Load `scripts/project.sh`; keep personal data private; sign commits and push.
- User is away and requests no questions: settle routine choices autonomously.

## Review Focus

- Internal upstream HID clears bypass the public dispatch wrapper: Task 3 tests
  separate objects and actual linked calls to both clear wrappers.
- A fast complete tap or queue overflow must not leave a remote key held: Task 2
  tests FIFO ordering and a reserved zero/resynchronization barrier.
- Radio ACKs lack IDs: Task 2 tests early/late ACKs, serialized controls, stale
  notifications and raw payloads resembling connection frames.
- LEFT radio mode must retain both halves while writes remain USB-only: Task 3
  tests scan/split availability independently of updater admission.
- Current Studio edits must survive restoring eight bindings: Task 5 backs up
  and compares every other binding and all names/order after save/reload.

## Task 1: Reclaim unused USB IN receive storage

**Files:** local `firmware/zmk/drivers/usb/device/usb_dc_wb32.c` and dedicated
USB driver fixtures under `tests/zmk/`.

**Interface:** unchanged USB device API and four OUT endpoint buffers of 64 bytes.

- [x] Add/run failing storage and independent OUT endpoint buffering regression.
- [x] Move RX bytes out of IN metadata, preserving behavior; format and run the
      complete real-driver USB fixtures. Expect at least 256 bytes less RAM.

## Task 2: Radio protocol and existing UART owner

**Files:** create `firmware/zmk/radio/core.{c,h}`, `radio.{c,h}`, `Kconfig` and
`CMakeLists.txt`; extend `firmware/zmk/battery/battery.c` and worker integration;
dedicated native tests `tests/zmk/radio_*` and runner.

**Interfaces:** pure core accepts immutable keyboard[8]/consumer[2], destination
0..6, time and parsed notifications, produces bounded complete TX frames and
typed status. Adapter owns locking; battery owner alone performs UART MMIO.

- [x] Write/run failing exact select/report/ACK/queue/timeout tests.
- [x] Implement bounded FIFO, release barriers, selection/pair state and frame
      scheduling, with one control/query outstanding and finite retries.
- [x] Integrate shared RX and TX without another UART/thread; preserve battery
      tests and add interleaving, raw quarantine, stall and status regressions.

## Task 3: ZMK routing, controls and physical split availability

**Files:** `firmware/zmk/radio/{endpoint.c,behavior.c,control.c,control.h}`,
binding YAML, Studio overlay/config; `wired/wired.{h}`/`wired_wb32.c`, lighting,
update receiver/service; dedicated routing/mode/hold/control tests.

**Interfaces:** `gmk_radio_select(channel, pair)`, immutable report submission,
cached status and worker poll; Studio `gmk_radio` label **GMK70 Bluetooth /
Dongle**, parameter 0..6. Physical switch getter returns raw 0..3; split-available
is separate from strict `gmk_wired_mode()` write authorization.

- [x] Write/run failing multi-object report/clear wrapper, hold cancellation,
      mode debounce, split and USB control validation tests.
- [x] Implement wrapper snapshots and zero reports, 3000 ms hold behavior,
      explicit bounded USB controls/status and mode-driven selection.
- [x] Keep RIGHT USB-mode split behavior compatible and verify all existing
      updater/settings gates; compile affected ARM targets and inspect RAM/calls.

## Task 4: Migration and Studio metadata

**Files:** `tools/keymap-migration/vial_to_zmk.py` and tests;
`tools/firmware-update/studio_rpc.py` and tests; new typed radio device helper.

**Interfaces:** symbolic `gmk_radio`: stock 0x7E00..04 maps to slots1..5,
0x7E05 to6, 0x7E06 to0; reject invalid/bool/extra parameters. Resolve the exact
runtime behavior display name without changing existing battery/RGB handling.

- [x] Write/run failing wireless translation and actual Studio framing tests.
- [x] Implement strict mappings/metadata resolution and validate preserved
      unsupported dispositions. Expect eight formerly unsupported bindings
      supported, five still unassigned in this user's private plan.
- [x] Format, lint, strict mypy and run full migration/firmware-update suites.

## Task 5: Review, guarded installation and physical verification

**Files:** separate private operators/evidence in `.work/zmk-wireless/` and
private recovery archive; public aggregate validation docs only.

- [ ] Run `make test test-zmk check-tools`, affected ARM builds and linked-image
      checks; fresh independent review; fix functional findings with regressions.
- [ ] Capture fresh clean Studio map and both-half health; freeze reviewed
      package/operator inputs; one guarded LEFT INSTALL and independent identity,
      resident, saved-map and health verification. No right-only operation.
- [ ] Restore only the eight source-export wireless positions in the fresh saved
      map; preserve every other binding/name/order; save/reload/compare.
- [ ] Reuse the existing Bluetooth bond for connection and narrowly filtered
      F24 input verification where possible; test dongle selection/frames and
      clearly record the absent receiver's validation limit; restore USB output.
- [ ] Reopen normal Studio, record exact five remaining unsupported bindings,
      archive private evidence, sign/push and update the goal honestly.

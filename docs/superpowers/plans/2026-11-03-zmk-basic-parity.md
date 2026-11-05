# GMK70 basic parity implementation plan

> Execute the independent hardware/control work in parallel, then integrate and
> obtain a fresh review of the whole change. Use failing dedicated tests before
> implementation and maintain `.work/zmk-basic-parity/progress.md` throughout.

**Goal:** Implement the audited basic GMK70 firmware features using native ZMK
and existing board hardware while preserving the updater, resident and one image.

**Architecture:** Native activity, HID indicators, mouse behaviors, settings and
reset APIs supply the common behavior. Board adapters supply WB32 low-power/wake,
RGB pixel composition, external-radio transport and factory bootloader wiring.
Keep existing workers and storage ownership; share runtime preferences.

**Tech stack:** Pinned ZMK/Zephyr C, native sanitizer/MMIO tests, ARM builds,
strict typed Python tools.

**Spec:** `docs/ZMK-BASIC-PARITY.md`.

## Global constraints

- No dependency, resident or saved-layout format changes; no permanent watchdog.
- One universal application with runtime role detection; never initialize LEFT
  devices on RIGHT. Keep all retained RAM reservations and application bounds.
- Runtime settings may save wirelessly only through the reviewed idle storage
  boundary. Updater and Studio authorization remains wired/USB/role gated.
- Inspect memory before flashing; do not shrink stacks merely to fit features.
- No INSTALL replay, speculative register sequences or destructive reset tests.

## Review focus

- A key/peer/cable edge during sleep entry must abort entry or remain pending.
- Held keys, radio packets, flash work and user pairing block sleep until safe.
- Unknown/stale battery information never masquerades as a critical reading.
- RGB feedback and temporary overlays preserve normal lighting and physical off.
- Wireless persistence never opens the updater or arbitrary settings-write gates.
- USB wake requires host authorization; recovery never clears unrelated storage.

## Tasks and ownership

### 1. Persistent controls and native indicators

Owned paths: new `firmware/zmk/controls/`, new dedicated controls tests.
Use native settings, layer APIs and HID indicator events. Provide one shared
preferences record for GUI lock, Mac mode, last BT slot and power preferences.
Expose the public integration APIs in a header and report them before callers
are edited. Provide modifier projection and a serialized RGB-overlay hook rather
than installing a duplicate HID getter or keycode wrapper.

- [x] Dedicated failing tests: reboot restore, invalid settings, GUI held state,
  native HID indicators, RGB/indicator precedence and physical roles.
- [x] Implement and pass the tests; format and native strict-warning compile.

### 2. Existing radio firmware transport

Owned paths: `firmware/zmk/radio/`, battery UART parser integration, dedicated
radio tests. Root integrates shared preferences and build configuration.
Reuse stock mouse/system packet facts and sleep-enable commands. Expose bounded
idle/flush/power state and host LED notification hooks. No new UART owner,
arbitrary command injector, pairing erase, or raw payload logging.

- [x] Failing protocol-faithful tests for mouse snapshots/releases, host changes,
  sleep-control ACK ownership, host LEDs and timeout/recovery.
- [x] Implement transport and pass existing/new radio tests.

### 3. USB and recovery integration

Owned paths: WB32 USB controller, new `firmware/zmk/recovery/`, dedicated USB and
recovery tests. Root owns build configuration and final storage integration.
Reuse native USB wake and reset/bootloader behavior boundaries. Use exact pinned
WB32 factory bootloader facts and a native retention/boot-mode adapter where
compatible. Provide safe settings-reset/startup recovery APIs, not storage wipes
inside IRQ/USB callbacks.

- [x] Failing MMIO/native tests for authorized USB wake timing, suspended states,
  preserved control transfers and exact bootloader magic/range handling.
- [x] Implement adapter and prove resident/retained-bank exclusion.

### 4. Coordinated native sleep and runtime persistence

Root owns `firmware/zmk/power/`, WB32 SoC/GPIO/scanner/split adapters, settings
backend, top-level CMake/Kconfig, board configuration and integration glue.
Use native activity/sleep policy with a board hardware backend and coordinated
peer control. Reuse QMK/vendor STOP/wake sequences with pending-edge protection.
Native RGB idle off, critical-battery handling and the external radio's low-power
protocol must cooperate; runtime writes use a safe shared storage boundary.

- [x] Add failing tests covering CPU/radio/split entry, wake arming races, no
  polling during sleep, both roles, busy guards and fresh battery thresholds.
- [x] Implement the hardware/native adapter and wireless runtime-save boundary.
- [x] Integrate shared preferences, controls, indicators, mouse and recovery.

### 5. Whole-image qualification and review

- [x] Run format, full native/repository suites, lint and strict typing.
- [x] Build affected ARM variants; record RAM/stacks/flash and unchanged resident.
- [x] Obtain fresh whole-change review and resolve material findings.
- [x] Preserve private device configuration before any separate installation;
  qualify hardware where the interface permits and report unmeasured limits.
- [x] Update handoff/validation with exact feature status and evidence.

The source implementation, offline checks and separate installation are complete.
Both joined halves now run the same 105,876-byte image with unchanged residents,
one acknowledged INSTALL each, automatic return, and four-layer/296-binding Studio
preservation. Each pre/post both-half health window passes at least 70 seconds
without retries or watchdog feeds. Fresh battery samples are healthy at 60%;
physical charging progression, Windows Bluetooth refresh and battery-only
sleep/wake/endurance remain unmeasured. Required GPG signing is still unavailable
(`No pinentry`); the reviewed source remains uncommitted with its patch/overlay
and exact packages frozen privately.

## Execution ledger

- Starting source: signed `e5e8a8dd365b132d58fe5714fac5a5b9f0b25f84`.
- Ruling: use the existing checkout on `codex/zmk-basic-parity`; cached builds and
  device evidence remain in the sole maintained GMK70 repository.
- Ruling: the user authorized execution of the audited list; proceed through
  implementation without another plan-selection or approval loop.

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

- [x] Add/run failing native tests: resident/app/unaligned/out-of-range writes rejected; allowed256-byte staging and commit pages; failed operations/readback mismatch; IRQ/FHSI/PCLK state restored.
- [x] Add/run failing watchdog tests for inherited running watchdog, finite readiness failures and correct long-period configuration/feed ordering. Staging builds must not refresh it.
- [x] Implement native adapters from pinned vendor sequences without changing common/QMK/resident code. No protection or option-byte changes.
- [x] Format and run native sanitizer tests; report tests and source provenance. Commit task files and review.

### Task 2: Wired matrix and Vial-compatible transport

**Files:** Create `firmware/zmk/wired/` C/header sources, native tests under `tests/zmk/`, and kscan integration/binding under `firmware/zmk/drivers/kscan/`, `firmware/zmk/dts/bindings/`. Generate native pin declarations from maintained `keyboard.json` with `firmware/zmk/tools/wired_pins.py`; do not add a third independently maintained copy of the matrix pins.
**Interfaces:** `bool gmk_wired_is_left(void)`, `bool gmk_wired_mode(void)`, `void gmk_wired_set_update_handler(void (*)(const uint8_t[32], uint8_t[32]))`, `bool gmk_wired_update(const uint8_t[32], uint8_t[32])`, `bool gmk_wired_remote_matrix(uint16_t rows[5])`. Native UART/kscan module starts automatically at Zephyr init; polling and RPC share a mutex. Handler must queue work and return promptly, never flash in serial processing. B9 high means left; C14/C15 both high means wired, matching maintained sources.

- [x] Derive exact19-entry installed Vial transaction profile from pinned source and full trial ELF; save reproducible compatibility evidence. Matrix payload is five little-endian uint16 rows; updater RPC ID18, RPC framing IDs13..16. The actual linked CRC8 uses initialFF, polynomial31 (not the optional upstream table implementation).
- [x] Write/run failing native tests for handshake XOR19, CRC8 from actual upstream implementation, RPC validation/bounds, partial frames/timeouts, wrong ID/lengths, stale output, and matrix disconnect releases.
- [x] Implement native WB32 UART1 A9/A10 1152008N1 full duplex with finite transactions and correct clock/pin/FIFO setup. Preserve vendor attribution.
- [x] Implement 5ms-debounced role-sensitive local matrix and combined10x9 kscan delivery, sharing existing maintained pin/key facts. Right serves matrix and updater RPC; left polls and forwards. No EEPROM/radio writes.
- [x] Run native tests and compile checks; commit task files and review. Root CMake/Kconfig/board hookup is Task3.

### Task 3: Native ZMK update receiver and raw HID service

**Files:** Create `firmware/zmk/update/` receiver/rawHID integration, its CMake/Kconfig and focused native tests. Fix Task1 architecture MMIO include integration and test stubs if required. Root module/board/build/host hookup belongs to Task4.
**Interfaces:** Consume Task1/2 exports. `gmk_wired_set_update_handler` registers a nonblocking protocol receive/cache function; a dedicated worker executes common protocol state machine and 300ms delayed install reset. Reuse normal updater namespace0x70/ABI1. Register raw HID1 before ZMK enables USB at APPLICATION96. No flash or blocking peer RPC in USB/IRQ callbacks.

- [x] Add/run failing tests for duplicate/busy/correlated requests, invalid/mutating USB role/mode gates, staging write exclusion, flash failure, reset after committed ACK and housekeeping timeout.
- [x] Add 32-byte raw HID1 alongside ZMK keyboard HID0; usageFF60/61. USB mutations require left role, configured USB, wired mode and verified permanent watchdog; right wire requests inherit master authorization. Staging qualification permits only local BEGIN clearing metadata and ABORT, with target1 read-only; never DATA/SEAL/INSTALL.
- [x] Runtime health namespace0x72/ABI1/op0x20 uses nonce8, magic0x4B4D5A47 at12, then uptime/feedcalls/flags/events at16; flags ready1/wired2/peer4/staging8. It is read-only on both targets, with normal updater INFO unchanged. Return cached responses without blocking serial callbacks.
- [x] Add optional permanent-only CONFIG_GMK70_ZMK_PEER_TRIAL_RELAY: direct common gmk_trial_packet_valid for namespace0x71/ABI2, target1 forwarding only, no local trial backend. Same USB role/configuration/wired/watchdog gates, separate namespace ACK cache, malformed/header/nonce/cache tests. Disabled in qualification builds.
- [x] Integrate early permanent watchdog init and live worker feeds/health; no feeds in staging. Define CONFIG_GMK70_ZMK_UPDATER and CONFIG_GMK70_STAGING_QUALIFICATION in update/Kconfig. Fix real ARM compile failure from missing zephyr/arch/cpu.h MMIO definitions in Task1 sources and cover native/real-header compilation.
- [x] Format/run focused sanitizer and actual-header object checks; report integration requirements. Commit and review the receiver before full-board integration.

### Task 4: Actual permanent board, reproducible build and host packages

**Files:** Create `boards/zuoya/gmk70_wired/`, build/package/image checking support and dedicated tests. Modify module CMake/Kconfig, `scripts/build_zmk.sh`, Makefile, host `tools/firmware-update/update.py`, new read-only status tooling and tests.
**Interfaces:** Consume Task1/2/3 report contracts. Host `profile=zmk` accepts only validated application-slot images and the exact resident hash, retaining debug/Vial support. USB VID342D PID E491 manufacturerHS, product GMK70 ZMK Wired, usageFF60/61; two HID instances. Define __gmk_image_end from the entire Zephyr load range (__rom_region_start + _flash_used), not just text. Preserve experimental trial targets.

Integration details: use `CONFIG_USB_HID_DEVICE_COUNT=2`, `CONFIG_ENABLE_HID_INT_OUT_EP=y` and `CONFIG_HID_INTERRUPT_EP_MPS=32`, preserving HID0 keyboard and HID1 updater. Source root kscan/update Kconfig and CMake integration only for the native wired target. Generate pins with an actual CMake dependency on the maintained keyboard JSON and generator. Keep the trial DTS and compiled images unchanged. The canonical stock Fn position is left row4/column2; use the maintained stock keymap as the layout reference. The exact padded resident SHA256 is `be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`, CRC1770733249; an existing pinned resident output is an explicit packaging prerequisite, never a reason to alter/rebuild the resident during a ZMK build.

Read-only health tooling repeats the same namespace0x72 nonce while BUSY; a fresh nonce requests fresh peer health. Never use namespace0x70 STATUS to poll namespace0x72. Validate response length, header, nonce, magic, role and permanent/staging flags; prove advancing modulo32 uptime/feed calls for at least60 seconds, with completed samples no more than2 seconds apart per half and no hidden reconnect/reboot. Begin the observation after uptime exceeds2 seconds so a brief initial restart cannot hide within a sample gap. Keep transport retries distinct from resets or stopped counters. Zero matrix-event count is unavailable, not physical key evidence. Include identity inspection and JSON evidence without configuration payloads. Add new Python tools to their existing uv project's strict checks and dedicated tests; integrate the native suites into `make test-zmk`.

- [x] Add/run failing build/image/package tests: wrong link range, wrong resident, malformed health replies, unsupported profile and accidental staging install all rejected.
- [x] Add universal board, actual10x9 keymap and function layer, handedness, wired switch detection, permanent watchdog, application-slot linking and bounded staging-qualification variant. Staging variant permits no running-code write or durable install.
- [x] Add explicit `make build-zmk-wired-updater` and staging qualification targets with separate caches/artifacts and clear labels. Existing trial builds remain unchanged. Package reviewed resident bytes from pinned QMK/Vial output with exact identity validation; builds never flash.
- [x] Add `wired-qualification` and `wired-update-test` variants alongside `wired-updater`. Product strings are GMK70 ZMK Wired Qualification, GMK70 ZMK Wired Update Test, and GMK70 ZMK Wired respectively. Update-test differs only in the product label; default permanent image stays normal. All use separate caches/artifacts.
- [x] Add `zmk` host package profile and migration/rollback tests, plus read-only namespace0x72 runtime health tooling for both halves. Retain resident/vector/CRC/role checks.
- [x] Validate actual ELF vectors/load segments, complete image-end symbol, combined matrix, receiver/native symbols and absent main-return wrapper in permanent target. Existing trial hashes must stay unchanged.
- [x] Run make test/test-zmk/check-tools and affected supported builds; review implementation and whole branch before hardware writes.

### Task 5: Staging proof, permanent installation and update round trips

**Files:** Separate ignored device scripts/reports; tracked `docs/ZMK-PERMANENT-WIRED.md`, validation evidence and updated handoff/status.

Runtime finding (2026-10-28): the qualification USB interfaces opened successfully,
but no INFO reply arrived. Pinned Zephyr initializes static threads after
APPLICATION initializers; the updater's earlier `k_thread_start` therefore did
nothing, and its later `SYS_FOREVER_MS` initialization left it asleep. Before
another trial, repair the service lifecycle with static autostart after successful
service initialization, a readiness guard that prevents processing/feeds on init
failure, and a regression that exercises the actual initialization order and a
queued raw-HID request. Review this production delta, rebuild/recheck A/B/Q, and
freeze a new device epoch. Preserve all three failed observer attempts and their
verified unchanged-Vial returns; do not reuse their failed chains for installs.

Device work uses the existing isolated Windows Python/HID runtime through left USB, both halves joined, normal switches; no driver installation or right-only cable/pad procedure. Freeze the reviewed A/B/qualification artifacts into the current device folder alongside the exact left/right Vial recovery packages and native backup recorded in its baseline JSON. Gate each dependent write on verified current application and resident identities. Device scripts are explicit operations, never called from build/test/setup, and require offline ordering/identity tests plus review before execution.

Native restart investigation (2026-10-28): initial Vial-to-A and subsequent
healthy native A-to-B transfers both failed automatic restart verification.
After each joined-left USB reconnect, the requested new application and unchanged
residents read back correctly and left health passed a fresh 60-second window.
Right remains Vial and must remain untouched until a different-image native left
update automatically restarts and passes health. Preserve both failed reports;
post-reconnect readback does not turn them into automated update successes.

Test one startup hypothesis before further migration: QMK explicitly drives USB
D+ low for 50 ms before connecting after reset; native startup has no equivalent
interval. Add a wired-board-only APPLICATION89 initializer that calls the existing
electrical disconnect helper, then sleeps 50 ms in the initialization thread,
before service90 and USB96. Keep the delay outside the USB driver's interrupt
lock. Exercise actual initialization registration and driver attach in a focused
failing regression, including enabled interrupts during sleep and exclusion from
legacy trial builds. Leave reset, oscillator, watchdog, flash and resident
behavior unchanged. This is a controlled single-variable hypothesis; require
review, rebuilt A/B/Q and actual automatic native restart evidence before calling
it a fix. Successful prior staging does not establish its cause either way.

Experiment outcome: the 50-ms startup interval did not restore automatic restart.
The new A read back correctly and passed fresh health after another joined-left
USB reconnect. Preserve that failed epoch and remove the isolated timing change
before testing another behavior. Primary-manual review did not establish a cache
or oscillator defect; do not change those registers speculatively.

The next concrete board omission is the USB pull-up supply control. Maintained
`config.h` defines USB_POWER_EN_PIN as B1; `gmk70.c` preloads it low before output
mode, and `drivers/wireless/transport.c` identifies its connect/disconnect hooks
as power control for the USB D+ pull-up resistor. Native ZMK never configures B1.
Add a wired-board-only initializer before USB96 using the existing GPIO driver
to configure B1 as GPIO_OUTPUT_LOW. Keep its source contract explicit and its
legacy exclusion tested; preserve unrelated pins. No high pulse, delay, clock,
watchdog, flash or resident change is justified. Test actual init registration,
GPIO configuration and attach ordering before a separately reviewed left-only
automatic restart experiment. The missing initialization is established in the
source; its causal role in the observed failures still needs hardware proof.

For left staging, coordinate the existing Vial trial client and a temporary qualification HID handle in one process: open the qualification handle only after its unique product enumerates, inspect local health/INFO and old right INFO, optionally BEGIN-clearing only isolated metadata then ABORT, and close it before Vial recovery observation. For right staging, native left USB stays connected: use one shared handle that dispatches namespace0x71 trial packets and namespace0x70/0x72 reads, observe immediately after the successful RUN acknowledgement during the bounded three-second trial, then restore the trial client's request correlation before normal recovery checks. Its existing nine-second post-RUN sleep would miss this window without the inline observer. Budget the existing USB exchange's two-second blocking bound; do not accept replies after observer deadlines. Confirm exact qualification size/CRC and staging health before any right application replacement. Left legacy Vial recovery requires its existing30-second observation; right health-capable Vial recovery requires60 seconds and advancing uptime/feeds. Never invent a retained record or weaken the watchdog recovery checks to advance migration.

- [ ] Freeze reviewed packages and resident identities; fresh backup and read-only running identities. Prove bounded left staging USB/receiver and right Vial transport; do not overwrite executing staging code.
- [ ] Install permanent left only; verify both INFO identities, stable uptime and unchanged residents before dependent writes. Retain right Vial while proving a different-image left ZMK self-update. Frozen Vial may need a power cycle after inherited IWDG; do not invent a recovery record or claim general warm rollback.
- [ ] Use native left's optional peer trial relay to launch right staging qualification while right still runs Vial. Observe actual right ZMK INFO/health and automatic Vial return using one coordinated host handle before any right application replacement.
- [ ] Install permanent right through left; verify both genuine ZMK applications, wired matrix/USB behavior and no reboot loop. Preserve resident/configuration evidence and physical limitations honestly.
- [ ] Prove right update and left self-update through left USB using normal/update-test product-label builds; an identical-image resident skip is insufficient. Install left normal then left update-test, qualify right staging, install right update-test, update right to normal, then left to normal. Verify both final identities/uptime/residents. Diagnose failures without blind retries or right-only recovery requests.
- [ ] Commit/push reviewed evidence and report current firmware, working features, deferred features and any unresolved limitation.

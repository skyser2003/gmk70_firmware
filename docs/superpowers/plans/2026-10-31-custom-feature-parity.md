# Repository Custom Feature Parity Implementation Plan

> **For agentic workers:** Use test-driven implementation and independent final review. The user explicitly requested implementation of the three audited leftovers.

**Goal:** Port the three remaining portable repository additions across VIA, Vial and ZMK.

**Architecture:** VIA and Vial use the same maintained board helpers. Preserve each pinned framework's native matrix/debounce/split processing; an awake-idle adapter skips local GPIO sweeps only when released and settled. Track pairing holds by physical position and cancel on live mapping changes. Gate ZMK LED supplies around completed pixel writes, preserving battery overlays and recovery.

**Tech Stack:** Pinned QMK/Vial, ZMK/Zephyr, maintained C and dedicated native regression fixtures.

## Global Constraints

- Pull `main` first, preserving local changes; the clean main checkout is already current at `a2f9efe`.
- Work in the existing checkout with the user's established main-branch workflow and pinned build caches. Do not create another GMK70 checkout or modify dependencies.
- No device operations, settings resets, resident writes or dependency changes.
- Preserve stock definition/custom keycodes, saved settings layouts, permanent ZMK no-watchdog policy, and retained RAM exclusions.
- Source `scripts/project.sh` in Bash before runtime commands. Format C, run strict native warnings, Python formatter/lint/typing where edited, complete repository tests and affected builds.
- Keep changes reviewable locally; this request does not require another firmware installation.

## Review Focus

- A key edge during idle arming must survive; held/debouncing inputs must keep local scanning active.
- Idle skipping must continue split transactions, peer releases, keyboard hooks and encoder maintenance on both roles.
- Awake IRQ ownership must yield cleanly to the existing STOP/wake machinery and recover after matrix reinitialization; conflicts fall back to polling.
- Editing or releasing a selector must cancel its pending pairing without another identically mapped key canceling a newer hold.
- Black frames and failed/retried pixel writes must handle LED rails safely; battery holds while RGB is off must light and then restore physical darkness on both halves.

## Task 1: Shared QMK/Vial idle matrix adapter

- [x] Add a dedicated native fixture with both half pinouts, actual native matrix/debounce integration, idle scans, first press, held/release debounce, arm-edge races, EXTI conflict, peer-only events and STOP/reinitialization recovery.
- [x] Run it against the current implementation and record the expected missing-idle failure.
- [x] Add the maintained adapter and common build integration; use `__wrap_matrix_scan` around native scanning and `matrix_post_scan` during idle so split maintenance remains native. An explicit optional `lpwr_matrix_idle_stop` hook yields awake IRQ ownership before native STOP preparation.
- [x] Add focused wake/EXTI integration tests; final ARM call-site verification is included in Task 4.
- [x] Format and run targeted tests.

## Task 2: Shared pairing-hold safeguards

- [x] Reproduce duplicate selector releases and live binding replacement with dedicated tests before changing production code.
- [x] Track a pending radio hold by physical position; release it from the early physical hook and cancel it on live keymap writes. Preserve 3-second timing, native authorization and physical switch gates.
- [x] Keep implementation shared between VIA and Vial; integrate native storage wrappers in common build rules.
- [x] Format and run targeted regressions, including unaffected host switching.

## Task 3: ZMK LED rail control

- [x] Add failing pixel/lighting regressions for black-frame power-off, enable-before-send, overlay release, per-half behavior, and write failure recovery.
- [x] Implement rail control in the existing serialized pixel owner; a completely black local frame powers off only after safe transmission. Nonzero frames restore rails before transmission and respect pixel reset timing.
- [x] Preserve native RGB configuration, battery overlay behavior, split replay and permanent updater policies.
- [x] Format and run pixel/lighting tests.

## Task 4: Integration and verification

- [x] Integrate build/test hooks and update relevant behavior documentation with hardware-validation limits.
- [x] Run `make test`, `make test-zmk`, `make check-tools` and edited-file formatting checks.
- [x] Build five supported QMK configurations, seven Vial configurations and seven wired ZMK configurations using the repository commands; confirm resident checksums and canonical RAM boundaries.
- [x] Obtain independent review, fix material findings and repeat affected verification.
- [x] Inspect the final diff for private data and unintended dependency/settings changes; report actual validation and remaining hardware checks.

## Implementation and review notes

- Observed the original native matrix fail the zero-idle-GPIO assertion. The
  final fixture runs both pinned frameworks, both pinouts and master/slave roles;
  it covers peer disconnect release, held/released debounce, a brief bounce,
  arming races, foreign EXTI ownership, timer wrap and STOP recovery.
- Pairing regressions initially failed 12 assertions across the two frameworks;
  all 22 final native scenarios pass, including native Vial storage authorization.
- Pixel regressions initially failed black-frame rail shutdown, actual overlay
  restoration and failed-init darkness. Final tests use the real pixel driver,
  lighting/battery overlay and split path with simulated MMIO.
- Independent review found two material matrix handoff problems. RED regressions
  reproduced each before correction: same-translation-unit STOP wrapping bypass,
  and split RX disabling C13's shared IRQ vector. An explicit STOP hook and
  ownership-checked retirement of all configured dormant STOP inputs fix them.
  Re-review found no remaining material defect.

## Completed validation

- `make test`, `make test-zmk` and `make check-tools` passed. Edited C passed
  clang-format; Python passed Ruff and strict mypy; edited shell scripts passed
  syntax checks. The optional built-resident integration also passed explicitly.
- All 19 firmware configurations built successfully. After the review fixes,
  affected early QMK/Vial builds were repeated; ARM disassembly for all twelve
  QMK/Vial images confirms the final STOP hook and native matrix/split call sites.
- All fourteen application packages pass offline checksum validation. Every
  padded resident remains CRC32 `1770733249`, SHA256
  `be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`.
- Canonical Studio is 82,800 application bytes and allocates 27,928 of 28,544
  RAM bytes, leaving 616 bytes. Both retained regions remain excluded; permanent
  watchdog and outgoing handoff remain absent.
- The original VIA definition and dependency pins remain unchanged. No private
  device data was added, and no connected-device operation was performed.

## Subsequent upstream integration

- Pulled `7a8d7da8fa68c4c1b1db9c50a69948c999536363`, adding Studio-editable
  knob positions. The only textual conflict was the ZMK README; both additions
  were retained. Before integration checks, all other local source/test files
  matched the pre-pull backup byte-for-byte.
- Independent review found no material interaction with the local idle,
  pairing or LED power implementations. Native tests additionally confirm that
  LED power cycling preserves encoder GPIO configuration and that overlays at
  positions 72/73 release local and peer power.
- Full ZMK tests, tooling/lint/strict typing and focused QMK/Vial matrix/pairing
  regressions passed. Upstream grows the Studio map to 74 positions while
  preserving the original 72 bindings per layer.
- Combined canonical Studio is 83,760 application bytes and allocates 28,136
  of 28,544 RAM bytes, leaving 408 bytes. The 208-byte RAM increase is from
  upstream knob support; retained regions and permanent no-watchdog policy
  remain unchanged.
- All seven integrated ZMK configurations rebuilt successfully. All six
  permanent packages pass application/resident checksum validation, and the
  optional built-resident integration passes. No production-code correction
  was needed; only the README merge, focused regression additions and these
  updated validation measurements differ from the pre-pull local work.

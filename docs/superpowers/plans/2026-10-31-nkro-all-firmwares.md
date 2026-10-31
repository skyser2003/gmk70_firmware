# NKRO Across GMK70 Firmwares Implementation Plan

**Goal:** Enable NKRO by default in VIA, Vial and ZMK, provide a toggle on Windows
and Mac function layers, and expose a remappable ZMK Studio behavior.

**Architecture:** Keep the pinned frameworks immutable. QMK/Vial use native
NKRO, native persistent toggles and `NKRO_DEFAULT_ON`. ZMK uses native extended
NKRO with a project adapter that limits the report to six ordinary keys when
toggled off, plus native USB boot-protocol fallback. Keep its descriptor fixed
so toggling does not require USB re-enumeration. Share the
stock-radio six-slot/overflow encoder across QMK and ZMK; its hardware bitmap
has 112 usage bits. Persist ZMK's toggle through the existing gated settings
backend. Preserve existing saved bindings and unrelated settings.

**Tech stack:** Pinned QMK, Vial, ZMK/Zephyr; project C adapters and strict Python
migration tooling. Source `scripts/project.sh` before build/runtime commands.

**User decisions:** NKRO defaults enabled in all three; add the ZMK toggle if
absent. Implementation choices below are routine project decisions. No device
installation, settings reset, commits or pushes are part of this work.

## Constraints and review focus

- Keep the earlier uncommitted safeguard synchronization intact.
- Preserve VIA JSON/custom numbers, resident bytes, native settings schemas,
  dependency pins and permanent ZMK no-watchdog policy.
- A saved off choice survives restart; absent/reset settings default on.
- Switching NKRO mode or host must release the outgoing format;
  overflow releases cannot be lost behind a full radio queue.
- Host-requested USB boot protocol uses the pinned native implementation.
- More than six keys, holes, high usages, modifiers, releases and reconnects
  need regression coverage. Do not claim RF delivery from native simulation.
- ZMK's canonical RAM capacity remains 28,544 bytes with both retained ranges
  excluded; measure the resulting allocation and image size.

## Tasks

- [x] QMK/Vial: test native default/persistence, both Fn+V bindings and live
  setter transitions. Replace forced startup NKRO with default-on behavior;
  preserve native toggle and live VIA controls.
- [x] Shared radio: extract a typed six-slot/14-byte overflow encoder, reuse it
  in QMK, extend ZMK's queue/frame/release path for A2 overflow reports, and
  test ordering, overflow, selection and recovery.
- [x] ZMK: enable native extended NKRO and boot support; add native Studio
  toggle behavior and both Fn+V defaults. Add runtime six-key report projection
  and gated debounced persistence without editing upstream.
- [x] Migration: recognize native NKRO on/off/toggle bindings as the new ZMK
  behavior and retain existing unrelated conversion behavior.
- [x] Integrate dedicated native regressions and build graphs; format, lint,
  strictly type-check, run full suites and build affected QMK/Vial/ZMK profiles.
- [x] Obtain independent review, verify linked hooks, resident identity and RAM,
  document behavior/limits, and leave the complete changes reviewable locally.

## Ownership and interfaces

QMK worker owns board `config.h`, `keyboard.json`, `gmk70.c` NKRO blocks,
`stock_keymap.h` and dedicated QMK NKRO tests. Radio worker owns wireless
encoding, its shared helper/build entry, ZMK radio files and radio tests.
Primary owns `firmware/zmk/nkro/`, USB descriptor integration, Studio behavior
node/config, service polling, migration, test orchestration and documentation.

- Shared encoder: `struct gmk70_radio_keys { uint8_t keys[6]; };`
  `void gmk70_radio_keys_encode(struct gmk70_radio_keys *, uint8_t modifiers,
  const uint8_t *bits, size_t bytes, uint8_t keyboard[8], uint8_t overflow[14]);`
- ZMK mode API in `firmware/zmk/nkro/nkro.h`:
  `bool gmk_nkro_enabled(void); int gmk_nkro_set(bool enabled);`
  `int gmk_nkro_toggle(void); void gmk_nkro_poll(bool writes_allowed);`
- ZMK radio adapter consumes native NKRO bodies and `gmk_nkro_enabled()`;
  mode changes release both classes and re-submit keyboard/consumer snapshots
  through `zmk_endpoint_send_report(7)` and `(12)`.

## Completed verification

- Full `make test`, `make test-zmk` and `make check-tools` passed. Edited C
  passed clang-format and strict native compiler warnings; Python passed Ruff
  and strict mypy. Shell syntax checks passed.
- All 19 affected configurations built: five QMK, seven Vial including the
  separate runtime qualification, and seven wired ZMK profiles including the
  Studio, peripheral and legacy wired variants.
- Canonical ZMK Studio is 81,384 application bytes and uses 27,792 of 28,544 RAM
  bytes, leaving 752 unallocated linker bytes. Both retained regions remain
  excluded; permanent watchdog and outgoing handoff code remain absent.
- ARM disassembly confirms USB send and GET_REPORT paths call both project
  keyboard/boot report adapters, and the existing worker calls NKRO persistence.
- Native-backed tests reproduced and fixed QMK flip-mask/cache release issues,
  ZMK's stale/underflowed boot-key counter, and queued consumer releases during
  mode changes. Independent review found no remaining actionable issues.
- All 14 packaged resident copies match the pinned padded SHA256
  `be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`
  and CRC32 `1770733249`; the optional built-resident integration test passed
  when explicitly enabled. The original VIA JSON and dependency pins are intact.
- No device operations, settings writes, flashing, commits or pushes were
  performed. Earlier source synchronization changes remain intact and local.

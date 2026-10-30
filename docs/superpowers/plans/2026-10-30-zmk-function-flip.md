# GMK70 Function/Media Flip Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline and
> superpowers:test-driven-development; one fresh final review.

**Goal:** Restore the persistent stock function/media flip and its one saved
Studio binding, preserving every unrelated feature and RIGHT firmware.

**Architecture:** A pure compact mapping/hold core, local keycode-event wrapper,
parameterless Studio behavior and existing-worker deferred settings persistence.

**Tech Stack:** pinned ZMK/Zephyr C, native sanitizers, strict Python/uv tooling.

**Spec:** [design](../specs/2026-10-30-zmk-function-flip-design.md).

## Global Constraints

- Existing branch/in-place execution follows the user's autonomy preference.
- Imported dependencies, factory keymap, RIGHT/residents, watchdog policy and
  thread stacks remain unchanged. Load `scripts/project.sh` before commands.
- Strict typing, private personal data, formatted C/Python, lint/type checks;
  build/test/conversion never access hardware. Sign commits and push.

## Review Focus

- Mode/OS changes while a source key is held must not leave its old target held.
- Repeated source presses after a mode change must release the old target first.
- Mac 5/6 change keyboard brightness only on press and emit no host key.
- Settings saves must not deadlock the updater worker or overwrite a newer toggle.
- A selective import must restore one binding and preserve all other 287.

### Task 1: Compact core and ZMK integration

**Files:** create `firmware/zmk/function_flip/{core.h,core.c,flip.h,flip.c,
behavior.c,CMakeLists.txt,Kconfig}`, behavior YAML; integrate root CMake/Kconfig,
Studio overlay/config, worker and `tests/zmk/` dedicated fixtures/runners.

**Interfaces:** `gmk_flip_core_event(struct gmk_flip_core *, uint32_t encoded,
bool pressed, bool mac)` returns typed current/previous output and brightness
action; `gmk_flip_toggle(void)` and `gmk_flip_poll(bool writes_allowed)`.

- [x] Write/run RED native tests for the complete stock tables, off/pass-through,
      modifiers, held releases across toggles/OS changes, duplicates and brightness.
- [x] Implement the pure bounded core; format and run sanitizer fixtures GREEN.
- [x] Write/run RED integration tests for the real wrapper, parameterless behavior,
      strict settings load, failed/coalesced save and changes during a save.
- [x] Implement the adapter using existing work/storage infrastructure; verify
      metadata on/off and real ARM wrapper calls, memory and stack limits.

### Task 2: Migration, review and guarded installation

**Files:** converter/Studio client and dedicated existing Python tests; build
checks and public validation docs, private hardware evidence outside Git.

**Interfaces:** symbolic `gmk_flip` with parameters 0/0, display name
**GMK70 Function / Media Flip**; existing `apply --only-behavior gmk_flip`.

- [x] Write/run RED translation and actual Studio codec/metadata tests; change
      only the obsolete unsupported-flip expectations, preserving other categories.
- [x] Implement translation/metadata and verify sparse application preserves287.
- [x] Run `make test test-zmk check-tools`, edited C formatting and affected ARM
      builds; request fresh review and fix important findings with RED→GREEN.
- [x] Sign source, fresh clean map/health, freeze one LEFT-only case, install once
      and independently verify the new image, unchanged RIGHT/residents/map.
- [x] Restore only the flip binding; save/reload/new-connection full comparison,
      normal Studio, health and private archive. Record hardware test limits.
- [x] Sign/push validation and report the four remaining unassigned positions.

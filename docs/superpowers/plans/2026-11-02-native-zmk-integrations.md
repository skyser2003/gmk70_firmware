# Native ZMK integrations implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task by task.

**Goal:** Reuse pinned ZMK implementations where compatible with the GMK70 hardware, updater, and identical firmware on both halves; qualify memory before hardware writes.

**Architecture:** Use upstream EC11 for quadrature decoding with deferred LEFT-only initialization and the existing Studio virtual keys. Use upstream split central event processing with a runtime board transport adapter where feasible. Preserve the existing wire framing for the updater, RGB pixels, legacy peers and matrix resynchronization: upstream UART transport has compile-time roles and no updater channel.

**Tech stack:** C11, pinned ZMK/Zephyr, native sanitizer tests, ARM builds.

## Constraints and review focus

- Do not modify pinned dependencies, resident, saved layout schema, host radio, or accepted NKRO toggle.
- Keep one image and runtime PB9 role detection; never initialize encoder pins on RIGHT.
- No permanent watchdog; do not lower stack reservations without measurements.
- Preserve knob direction, ordering, releases, mode gates and overflow behavior.
- Preserve RIGHT updater relay and legacy-peer compatibility throughout migration.
- Check native split source handling, duplicate RGB execution and RIGHT local event suppression.
- Build and report RAM including stacks before considering a separate device operation.

## Task 1: Native encoder

- [x] Add failing tests against the actual pinned EC11 driver for direction, reversals and queue overflow.
- [x] Replace the custom Gray-code decoder with a native sensor adapter and deferred EC11 device.
- [x] Preserve the two appended Studio keys and existing pulse/release handling.
- [x] Run encoder/WB32/kscan tests and ARM build.

## Task 2: Native split integration feasibility

- [x] Implement and exercise a runtime board adapter to the upstream central event core if it preserves existing behaviors within memory.
- [x] Test LEFT/RIGHT roles, legacy snapshots, disconnect releases and central-only board behaviors.
- [x] Document precisely which transport elements remain board-specific and why.

## Task 3: Qualification

- [x] Format edited sources and run repository, ZMK and strict tooling checks.
- [x] Build affected canonical ZMK distributions and compare ELF RAM and stack reservations with the baseline: 28,200 / 28,544 bytes, 14,336 bytes of stacks.
- [x] Obtain a fresh review of the complete change and resolve material findings.
- [x] Record results and any hardware validation still required. Device writes remain a separate operation after memory qualification.

## Execution ledger

- Baseline: clean main at fccfead46bc22627c470fd4b3d81f3cc1900d26f; current canonical Studio image has 344 bytes unallocated RAM.
- Ruling: preserve the legacy board wire protocol during native integration because it carries the updater, pixel frames and self-repairing matrix snapshots, and supports an unchanged peer during staged updates. Direct upstream UART transport does not preserve those contracts or runtime roles.
- Encoder: pinned-driver tests passed, including the native invalid-transition policy; board IRQ/fallback/both-role and virtual-key release tests passed.
- Split: both unmodified generic cores linked; exact source identity, deferred behavior codec/CRC/admission, old USER_SYNC shapes and separate updater RPC tests passed.
- Qualification: all seven affected ARM variants built, full repository and ZMK tests and strict tooling checks passed. Studio reserves 28,322 / 28,544 bytes, including unchanged 14,336-byte stacks. Fresh independent review found no unresolved material issue.
- Source provenance: new commits must remain signed under the user's existing instruction. Noninteractive signing found an expired/unavailable cache; do not create an unsigned commit or prompt. Freeze the reviewed working tree and record its base commit plus exact tree/archive hashes for device qualification.
- Hardware: both halves installed the identical 88,424-byte / CRC32 192780560 image with one INSTALL each through joined LEFT USB; residents unchanged, no manual recovery. Studio independently preserved all four layers and 296 bindings after each install.
- Qualification exception: first post-RIGHT strict observer rejected its unsaved report. No write/reset was repeated; a fresh diagnostic and final strict 70-second window both passed with zero retries/reconnects/feeds. Precise cause of the first rejection remains unknown and is documented, not hidden by the later checks.

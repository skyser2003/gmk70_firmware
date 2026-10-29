# ZMK resident handoff implementation plan

> **For agentic workers:** Use superpowers:executing-plans for inline implementation and a fresh final review. Do not operate devices during build/test.

**Goal:** Test an application-only alternative to the failing update reset.

**Architecture:** An explicitly selected development build quiesces USB, DMA and CPU interrupt state, then branches to the checked, unchanged resident. Existing variants retain SYSRESETREQ. This is an update activation workaround, not a diagnosis of hardware reset retention.

**Tech Stack:** Maintained C/Thumb assembly, pinned Zephyr/ZMK, native sanitizer fixtures.

**Spec:** `docs/superpowers/specs/2026-10-29-zmk-resident-handoff-design.md`

## Global constraints

- Resident CRC32 1770733249 and bytes remain unchanged; no radio/option-byte changes.
- Existing manifests, application/settings bounds and staging qualification remain unchanged.
- Device commands remain separate; first target is LEFT and failure stops later writes.
- Work continues on the existing development branch in the sole maintained repository.

## Review focus

- Inherited watchdog/clock configuration must be checked, not assumed from feed-call counts.
- Active DMA cannot survive replacement of the application's SRAM.
- All old-application code must stop before resident overwrites its flash pages.
- CPU thread/privilege/mask and stack changes need linked ARM evidence.
- A returning USB device must pass image/resident readback and both-half health.

### Task 1: Optional handoff backend and native tests

Files: `firmware/zmk/update/resident_handoff.{c,h,S}`, `service.c`, `Kconfig`, `CMakeLists.txt`; native fixtures under `tests/zmk/`.

- [x] Write/run failing service tests: selected permanent path calls handoff; ordinary and qualification paths still reset.
- [x] Test rejection of wrong resident, wrong vectors, wrong clocks/watchdog and non-thread/unprivileged contexts before detaching.
- [x] Implement `bool gmk_zmk_resident_handoff(void)`; rejection returns false, admitted path never returns. Verify detach interval, mask/SysTick/NVIC/DMA ordering and final watchdog reload with a simulated MMIO fixture.
- [x] Add stack-free ARM tail, inspect linked instructions and ensure it branches to the installed reset vector.
- [x] Run native suites and commit only verified software.

### Task 2: Separately labeled build and package

Files: `scripts/build_zmk.sh`, board `.conf` files, `firmware/zmk/tools/check_image.py`, `tools/firmware-update/zmk_package.py`, Makefile and focused tooling tests.

- [x] Add `wired-studio-handoff` and distinct-product `wired-studio-handoff-test` experimental variants, with Studio settings reservation and explicit handoff option.
- [x] Verify packaging retains LEFT-only `zmk-studio` profile and unchanged resident.
- [x] Compile both variants and existing supported affected variants; run `make test`, `make test-zmk` and affected Python formatter/mypy/tests.
- [x] Obtain a fresh code review before any hardware operation.

### Task 3: Hardware qualification or precise limitation

- [x] Freeze current evidence/candidate bytes and preserve settings before any write.
- [x] Inspect current identities, residents and healthy both-half operation.
- [x] Install LEFT only once. The old sender may still require the established joined-left cycle; do not replay INSTALL after timeout.
- [x] If reachable and healthy, test distinct-image handoff update; require automatic return and exact readback plus 60-second both-half health.
- [x] Preserve failures and stop dependent writes. Record actual result and distinguish bootstrap reset, handoff updating and ordinary hardware reboot.

## Progress

Tasks 1/2 are implemented and reviewed. Fresh baseline health passed 60.015
seconds with no retries/reconnects. Native `make test-zmk`, `make test` (88
repository tests plus native/migration fixtures), and updater Python tests
(113, one optional integration skip) pass. Changed Python files pass Ruff and
strict mypy. Full updater mypy also reports five pre-existing TypedDict.update
keyword errors in `tests/test_studio_transport.py:69`; these are outside this
change. Default normal/Studio/qualification ARM builds retain their previous
image sizes and no handoff backend. Both opt-in ARM builds/package checks pass.

Review corrected an APB1 DMAC1 address and independent negative-test fixtures;
both fixes have observed failing/passing tests. Mandatory handoff tests now use
synthetic non-executable admission data and require no ignored QMK artifact;
`GMK70_TEST_BUILT_ZMK_RESIDENT=1` additionally passed against the actual resident.
Linked ARM inspection confirms a stack-free final transition. Known upstream
RWX load-segment linker warnings remain unchanged.

Ruling: suppress peer RGB in these LEFT-only experiment variants because the
installed RIGHT predates native lighting frames. The default RGB build is
unchanged. A failing/passing local-only transport test proves zero peer writes.

Final opt-in candidates before hardware: A 71684 bytes/CRC32 2883208108, SHA256
`32b797e270659eb0ab0bd1c0ac70d964054188cd4f607a3af2bf1a383c8ec024`;
B 71692/3407121084, SHA256
`ecdcda6e6fc605b76472475bc5770dfdd92d0307f23c312b9d637f8c7b7e08f7`.
Both retain resident CRC32 1770733249. Task 3 bootstrap was acknowledged, but
old-sender SYSRESETREQ did not return USB for 80.125 seconds. The user confirmed
one joined-LEFT power cycle; initial readback then verified A, unchanged RIGHT,
and unchanged residents. Bootstrap health passed 61.000 seconds. Actual new-path
A→B→A activations returned automatically; B passed 60.969 and final A 60.046
seconds of both-half health, all retries/reconnects zero. RIGHT uptime continued,
its image stayed unchanged, and both resident CRCs remained 1770733249. No
further user cycle was requested/reported. Final Studio RPC verified four
72-position layers, preserved base bindings/saved Fn name and no unsaved
changes; normal Studio was reopened/reconnected. See
`docs/ZMK-RESIDENT-HANDOFF.md` and its sanitized result. This completes the LEFT
activation experiment; ordinary SYSRESETREQ diagnosis and RIGHT handoff remain
outside the result.

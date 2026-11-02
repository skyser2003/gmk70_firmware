# Native ZMK updater wire implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement and verify each task in order.

**Goal:** Carry updater requests/replies in native ZMK frames, preserve the resident and one universal image, then install and verify both halves through joined LEFT USB.

**Architecture:** Link the unchanged pinned `wired.c` parser and Zephyr CRC/ring-buffer implementation. Add board extension command/event messages containing the existing 32-byte updater packet. Reuse the board UART, mutex, responder thread and service callback. Keep matrix/RGB and behavior RPC17 unchanged. Canonical builds exclude updater RPC18; a separately named bridge build discovers native capability through a harmless RPC17 query and supports legacy peers during migration.

**Constraints:** No dependency edits, resident changes, permanent watchdog, saved-layout changes, smaller stack reservations, automatic INSTALL retries or fallback after an uncertain native request. Measure ELF RAM and stack reservations before device operations. Keep host USB and updater state machine unchanged.

**Review focus:** Corruption of every header/payload/checksum byte; truncated frames; wrong source/direction; old-peer discovery without sending native bytes; no duplicate command execution; callback execution outside flash work; both-half role/mode gates; native-only package proof; compatibility bridge available for future cross-stack migration.

## Tasks

- [x] Write failing native-frame round-trip/corruption/truncation tests using actual pinned ZMK parser, Zephyr ring buffer and CRC32.
- [x] Implement native frame codec/exchange/responder and safe capability discovery; preserve operation/session/offset and existing admission checks.
- [x] Integrate UART dispatch and native-only/bridge selection, with tests proving no fallback/replay after uncertain native delivery.
- [x] Add explicit Studio bridge build, linked-image checks and dedicated source/tool tests. Format, strict-check, run full relevant tests and build all affected variants; record RAM and stack frames.
- [x] Obtain fresh review; freeze source and packages. Back up Studio and qualify both halves.
- [x] Install LEFT bridge, RIGHT bridge, LEFT canonical, then RIGHT canonical via the native-only LEFT. One INSTALL per distinct image/half; independently qualify and compare saved Studio mapping between steps.
- [x] Record exact installed identities, unchanged residents, health windows, memory measurements and remaining limits.

## Execution ledger

- Starting installed image: both halves 88,424 bytes / CRC32 192780560; resident CRC32 1770733249. Starting source is the existing reviewed, uncommitted native-core integration on `codex/zmk-native-integrations`; keep it intact and avoid unsigned commits.
- Ruling: this task migrates updater relay to native ZMK framing; normal matrix/RGB and behavior RPC17 remain board transports. Native command extension type 0x80 and peripheral event extension type 0x80 carry the existing updater request and reply respectively. Native parser/CRC32 code is reused unchanged.
- Ruling: native-only canonical firmware is installed only after both halves support native updater messages. An explicit bridge artifact remains available to migrate back to older QMK/Vial peers without right-only recovery.
- Ruling: install canonical LEFT before canonical RIGHT so the final RIGHT firmware transfer is provably native: the sending LEFT image cannot link legacy updater RPC18.
- Review: corrected legacy shared-scratch and asynchronous INFO assumptions using a validated read-only STATUS service probe. Actual stale scratch and INFO-BUSY/STATUS-complete regressions pass; fresh reviewer confirmed both findings resolved.
- Offline qualification: all eight ARM variants built; full make test/test-zmk/check-tools and explicit built-resident pin check passed. Canonical 89,308 bytes / CRC32 599821369; bridge 89,632 / 501955662. Both use 28,322 / 28,544 RAM bytes, including unchanged 14,336-byte stacks. Native receive frame152/parser40, bridge120 bytes of individual compiler stack frames; no extra permanent stack/heap.
- Preflight: both live halves remain 88,424 / 192780560, residents1770733249. Fresh private Studio snapshot is clean, four layers/296 bindings. Signing preference remains in force; freeze the working tree/archive rather than create an unsigned commit.
- Deployment: all four INSTALLs were dispatched once. Both final applications independently report 89,308 / 599821369, residents1770733249; final strict both-half health70.171sec and Studio296-binding verification passed. No manual recovery or layout write.
- Exceptions: first step2 post-health report rejected, then fresh diagnostic and strict checks passed. Final step4 INSTALL ACK mismatched; no INSTALL replay, and independent exact-image/read-only health proved successful installation. Original Reply fields absent; a transient relay-failure/UNAVAILABLE path is possible, not established. Detailed report retains both exceptions and unmeasured stack/physical limits.

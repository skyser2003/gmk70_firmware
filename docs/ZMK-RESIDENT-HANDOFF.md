# Experimental application-only update activation

Current checkpoint: [permanent watchdog removal](ZMK-PERMANENT-WATCHDOG-REMOVAL.md)
applies to canonical builds and both installed halves: LEFT 70,196/986599886,
RIGHT 38,160/1887741245, resident CRCs 1770733249. Both passed 70-second zero-feed
health; Studio saving and the original four-layer map were verified. RIGHT
needed one joined-LEFT cold cycle for the old-watchdog transition. Earlier
experiment/build/state notes below are historical; use the linked build targets.

Earlier 2026-10-29 checkpoint: [LEFT watchdog rollback](ZMK-WATCHDOG-ROLLBACK.md) passed two
automatic original-SYSRESETREQ updates and Studio save/reload. Installed LEFT is
70,340/1796245169 with no watchdog; RIGHT and residents are unchanged. This
supersedes the installed-state notes below. The exact reset/retention mechanism
remains unmeasured; ordinary watchdog-enabled variants are unchanged.

The 2026-10-29 investigation adds separately labeled LEFT Studio builds that
can enter the **unchanged resident installer directly** after an update. This
is an experimental workaround for update activation; ordinary SYSRESETREQ
remains unchanged and its failure has not been localized. See the
[reset analysis](ZMK-WARM-RESTART-ANALYSIS.md) and
[design](superpowers/specs/2026-10-29-zmk-resident-handoff-design.md).

## Software contract

- `make build-zmk-wired-studio-handoff` builds A; the corresponding
  `-test` target builds B with a different product string. Outputs live under
  `artifacts/zmk/wired-studio-handoff{,-test}/`. These remain unlocked development
  `zmk-studio` packages, restricted to LEFT by the existing host validator.
- Ordinary builds and staging qualification retain their original reset path.
  A CRC-covered 16-byte native-startup footer permits handoff only to newly
  compatible receivers. Older ZMK and QMK/Vial images use SYSRESETREQ.
- Admission checks the full padded resident CRC, actual committed/staged image,
  footer, Thread/privilege state, boot magic, PLL/bus configuration and settled
  longest watchdog configuration. No resident, radio or option bytes change.
- USB detaches for 50 ms before scheduling/interrupts stop. UART IRQ generation,
  SysTick, pending CPU exceptions and NVIC banks are cleared. DMA controllers
  stop with readback before application SRAM can be replaced. A stack-free
  Thumb tail loads the resident VTOR/MSP/reset vector with interrupts masked.
- The resident validates/copies/clears the existing manifest and jumps into the
  receiver. Native startup normalizes inherited clocks and peripherals. The
  long watchdog is refreshed once before handoff and remains active through it.
- Partial preparation failures end in the ordinary terminal hardware reset;
  they never resume an old worker with partially disabled peripherals.

These LEFT-only experiments render local RGB but suppress peer lighting
traffic because the installed RIGHT predates the lighting transport. Four-layer
and compact-settings source from the [offline Mac/RGB candidate](ZMK-MAC-LIGHTING.md)
is included; this is not qualification of that candidate on both halves.

## Verification and current hardware boundary

Native handoff tests cover admission, independent corrupted inputs, boot magic,
failed DMA completion and ordering. Mandatory tests use synthetic non-executable
CRC admission data; `GMK70_TEST_BUILT_ZMK_RESIDENT=1` also exercises the actual
pinned resident. Linked ARM inspection confirms the stack-free tail and footer
after all initialized load sections. Native ZMK, repository and updater runtime
tests pass. Changed Python files pass Ruff and strict mypy; the full updater
type-check additionally exposes five pre-existing keyword-update errors at
`tools/firmware-update/tests/test_studio_transport.py:69`.

Initial A: 71,684 bytes, CRC32 `2883208108`, SHA256
`32b797e270659eb0ab0bd1c0ac70d964054188cd4f607a3af2bf1a383c8ec024`.
B: 71,692 bytes, CRC32 `3407121084`, SHA256
`ecdcda6e6fc605b76472475bc5770dfdd92d0307f23c312b9d637f8c7b7e08f7`.
Both require resident CRC32 `1770733249`, padded SHA256
`be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`.

Before installation, installed LEFT 61,696/`1010245074` and RIGHT
36,576/`1296309606` passed 60.015 seconds of fresh both-half health with zero
transport/relay retries or reconnects. Studio's current map was saved, read
through actual RPC (two layers of 72 bindings), and frozen privately with both
candidate packages. The application/settings boundary remains unchanged.

The one-shot **LEFT bootstrap INSTALL was acknowledged**, result 0/state 3 with
the expected A size/CRC. This ACK does not establish resident copy completion,
application entry or automatic restart. That transition uses the old sender's
SYSRESETREQ implementation; the new handoff cannot execute until A is running.
The bootstrap's old reset did not return USB within 80.125 seconds (80 failed
read-only discovery attempts; separate Windows metadata found no GMK70 HID or
USB interface). After the user's confirmed joined-LEFT power cycle, fresh
readback reported A 71,684/`2883208108`, unchanged RIGHT
36,576/`1296309606`, and both resident CRCs `1770733249`.
Post-cycle A passed 61.000 seconds of both-half health. The first actual new-path
A→B update returned the correct B identity automatically; B then passed 60.969
seconds of both-half health. Both health windows had zero transport/relay
retries or reconnects. RIGHT uptime continued across LEFT activation.
The B→A return update also returned the exact A identity automatically and passed
60.046 seconds of both-half health with all retry/reconnect counters zero.
No additional user power cycle was requested or reported between these updates;
RIGHT uptime continued across both LEFT activations. RIGHT received no application
write. Both final resident CRCs remain `1770733249`.

Actual Studio RPC read all four 72-position layers, behavior metadata and a clean
unsaved-changes state. All 72 original base bindings and the saved Fn layer name
survived. The default base name is now Windows; previously unsaved Fn defaults
adopt the Mac/RGB candidate's new controls. This is not a claim that the entire
old Fn default map stayed identical. Studio was reopened normally without the
temporary loopback inspection transport and connected again to COM3.

The [sanitized hardware record](validation/2026-10-29-zmk-resident-handoff/result.json)
keeps the failed bootstrap reset separate from the two successful new-path
activations. This qualifies LEFT updates between these marked experimental
images; it does not qualify ordinary SYSRESETREQ, RIGHT handoff or every future
firmware/clock configuration.

Private frozen inputs, configuration and per-stage evidence are under
`artifacts/recovery/2026-10-29-resident-handoff/operator-and-evidence/`, with the
live guarded workspace retained at `.work/zmk-warm-restart/`. Current hardware
status must be checked before resuming. Visual RGB output was not observed;
this experiment's hardware claims cover activation, health and Studio only.
Source implementation is commit `cc8f290` on the existing development branch.

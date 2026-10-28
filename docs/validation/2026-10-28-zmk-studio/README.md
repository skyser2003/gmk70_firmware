# Wired Studio qualification — 2026-10-28

The installed Windows ZMK Studio app connected to the GMK70 through standard USB
CDC (COM3). It displayed the 72-key physical layout and Base/Fn layers. The final
image is built from `f6cbeda`; [result.json](result.json) records sanitized facts.

Only LEFT was installed: first the reviewed Studio image 8e24ae72… (CRC 551392334),
then a distinct build 1aa74678… (CRC 1010245074), whose only additional firmware
change names the device GMK70 ZMK Studio. Both packages use the same native
updater and pinned resident. The second update was sent through the Feature HID
updater while Studio's serial connection existed. Each complete upload/SEAL was
followed by exactly one INSTALL. Both installs acknowledged committed state 3;
USB remained absent after 80 seconds, and each needed a user-confirmed 5-second
joined-left power cycle. No INSTALL was replayed. Automatic warm restart remains
unresolved.

The real app/client read an unlocked settings backend, physical layout and both
72-binding layers. It saved a temporary Q binding on unused macro position 16 and
a 19-character Fn layer name, discarded RAM changes and reloaded both from flash.
After the distinct-image update and power cycle, both edits reappeared in the
actual app. The original binding/name were saved again; a complete reread matched
the entire original keymap with no unsaved changes. Closing/reopening only the
Studio serial connection also displayed the restored Base/Fn map. No further
firmware install was needed. The app was then reopened normally without the
temporary loopback debugger, and Windows UI Automation reconnected COM3 and
confirmed the device name and restored Base/Fn layers.

Fresh readback and 61 seconds of final both-half health verified LEFT 61,696 bytes /
CRC32 `1010245074`, RIGHT 36,576 /`1296309606`, and resident CRC32 `1770733249` on
both. Uptime/watchdog feeds advanced with sample gaps below 2 seconds, zero
transport/relay retries and no observation reconnects. RIGHT firmware was never
written, and no right-only USB or pad procedure was used. The first Studio image
also passed 62 seconds of both-half health while the app was connected.

## Offline validation and review

- `make test`: 7 migration and 88 repository tests plus native suites passed.
- `make test-zmk`: native USB/clock/wired/update/receiver/settings/Feature suites
  and 10 Python tests passed. The two new tool projects passed formatter/Ruff and
  strict mypy; 113 updater Python tests passed with one optional built-resident
  integration test skipped.
- Actual Studio, normal updater and staging-qualification images compiled against
  exact clean dependencies. The final Studio ELF uses 61,696 bytes flash and
  26,632 bytes static RAM, fits 104KiB, preserves the retained RAM boundaries and
  contains the wrapped HID registration and native settings/CDC integration.
  The linked descriptor budget is 3 IN/1 OUT; Windows enumerated keyboard,
  consumer, vendor Feature and CDC interfaces successfully.
- A fresh read-only whole-feature review found a valid 31-byte framed layer rename
  overflowing the pinned 30-byte RPC RX ring. A regression through the actual
  pinned UART callback reproduced dropped bytes. Setting RPC RX 512 to match CDC
  RX 512 passed maximum-ID/escaped/binding requests and a complete 512-byte burst,
  followed by the full suites and actual Studio save/update checks.

## Decisions and remaining limits

The sole existing checkout was retained, with inline implementation and one fresh
whole-feature review. This respected the existing source boundary and autonomy
preference; an incorrect isolation choice could have caused integration rework,
bounded here by review, tests and frozen operator inputs.

Unknown nonblank settings are left locked rather than silently formatted. A cut
during first provisioning can therefore require explicit settings recovery. The
current device mounted and saved successfully, and actual behavior-ID/keymap
loading survived the distinct-image update. Individual setting durability is
covered by append/compaction fault injection; an entire multi-setting Studio Save
is not claimed atomic.

The review deliberately left physical storage, OS/app behavior, input and
silicon timing to qualification. This run established storage readiness, app
connection, persistence and runtime health. It did not inject real brownouts or
observe new physical typing on the Studio revision; zero matrix counters mean
unavailable, not a typing result. An unmeasured hardware failure could still
require LEFT recovery. RIGHT preservation is enforced by the operator target and
verified by unchanged identity.

Deferred minor coverage: a dedicated Feature-to-legacy reconnect regression and
broader native settings-dispatcher/behavior-ID/subtree/direct-load tests. The
existing tests cover forward migration and each transport; actual hardware
covered behavior-ID/keymap reload and Feature-to-Feature update persistence.

Private operator inputs, complete RPC/keymap records, screenshot and raw USB/
health evidence are preserved locally under `artifacts/recovery/2026-10-28-studio/`.
The separate pre-Studio A recovery package remains untouched.

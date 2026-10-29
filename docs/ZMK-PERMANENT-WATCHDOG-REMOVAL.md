# Permanent watchdog removal

The subsequent [right RGB restoration](ZMK-RIGHT-RGB.md) updates both installed
images while retaining this no-watchdog policy and the same residents. The
hardware results below record the original watchdog-removal installation.

On 2026-10-29 the user chose USB power cycling as the recovery fallback and
requested permanently removing the added long watchdog. Source `89784be` removes
the adapter and every permanent initialization/feed call, rather than retaining
an experimental toggle. The short watchdog protecting temporary staging trials
remains unchanged in the separate Vial trial launcher.

## Firmware and build contract

Permanent ZMK now uses ordinary SYSRESETREQ for update activation. The outgoing
resident-handoff workaround is removed. The incoming native-startup capability
footer remains for migration from older compatible senders; it does not enable
an outgoing handoff. Linked-image validation rejects watchdog and handoff symbols.

Update admission uses actual service readiness plus the existing wired, USB,
local/peer role and storage gates. Studio still uses the same worker-owned
settings journal. Health ABI1 retains a zero feed-count word and clears the old
watchdog-ready bit; bit 4 identifies intentional no-watchdog operation on either
half. The host still reads legacy watchdog health during migration, but final
device qualification explicitly requires bit 4 and zero feeds on both halves.

Canonical builds are `make build-zmk-wired-studio-updater` for LEFT and
`make build-zmk-wired-peripheral` for RIGHT. The subsequent RGB correction
enables native split lighting in both canonical profiles. Other normal wired builds also omit the
watchdog. Old handoff/watchdog-rollback make targets print a retirement notice
and build their canonical no-watchdog Studio counterpart; they cannot re-enable
it. All remain experimental/unlocked development packages, not published releases.

The resident, radio firmware, option bytes and external Vial configuration are
unchanged. Build/test commands never operate the device.

## Verification and recovery boundary

Native service tests require no watchdog calls and exercise readiness/worker
startup failures; receiver and settings tests preserve role/wired/USB/storage
rejection boundaries. Platform MMIO tests reject IWDG register accesses. Linked
canonical Studio, peripheral, normal, Studio test and qualification images contain
no permanent watchdog or outgoing handoff symbols. Repository tests, native ZMK
fixtures and 14 Python tests, and 115 updater tests (one optional integration skip)
pass. Changed Python passes Ruff, formatting and strict mypy. Independent code,
linked-image and separate one-shot device-operator reviews passed before writes.

Without a watchdog, a hung application can require a physical power cycle.
This is not automatic firmware rollback or a guarantee that every bad image is
recoverable merely by reconnecting USB. In the tested joined setup, the earlier
confirmed five-second LEFT USB cycle restarted both halves: prior RIGHT uptime
6,124,726 ms became 21,114 ms alongside LEFT 21,106 ms. If battery/power topology
changes, a LEFT USB cycle may no longer cold-start RIGHT. No pad bridge or
right-only flashing is part of this migration.

## Hardware result

| Half | Canonical variant | Size | CRC32 |
| --- | --- | --- | --- |
| LEFT | `wired-studio-updater` | 70,196 | `986599886` |
| RIGHT | `wired-peripheral` | 38,160 | `1887741245` |

LEFT SHA256: `548c87faa1676093fa9a87948203ba28162de36cce4d1132b94ef4794f272334`.
RIGHT SHA256: `9af105e35536f32c4aa614e14e4d838a639073dc7d42920ee9fe16e733cc157a`.
Both resident CRC32s remain `1770733249`.

Exactly one INSTALL was sent to each half. LEFT returned automatically and passed
70.109 seconds of both-half health with zero retries/reconnects. RIGHT acknowledged
its update but did not verify within 81.937 seconds. That old-to-new warm restart
remains a failure. After one user-confirmed five-second joined-LEFT USB cycle,
both exact new identities returned and passed 70.125 seconds of continuous
health: both bit 4 set, watchdog-ready clear, zero feeds, zero transport/relay
retries and no observation reconnects. No right-only USB connection, pad bridge,
resident write or repeated INSTALL occurred.

The new RIGHT then accepted BEGIN plus 256 bytes of staging DATA through LEFT
USB, followed by ABORT. Application/resident identities stayed unchanged and both
receivers were idle. This proves the new peer's update write admission without a
watchdog; it is not a second firmware installation or a future RIGHT warm-reset
qualification.

Actual Studio RPC confirmed the original four layers of 72 bindings. A temporary
Fn layer-name edit was saved, discarded/reloaded, then restored, saved and
reloaded again. The complete original map matched the pre-install snapshot and
no unsaved changes remained. Normal local Studio was reopened without remote
debugging and connected to COM3; final identity/health was checked again.

See [sanitized validation](validation/2026-10-29-zmk-permanent-watchdog-removal/README.md).
Private packages, map snapshots, reviewed one-shot operator, failed/successful
observations and a SHA256 inventory are kept under
`artifacts/recovery/2026-10-29-zmk-permanent-watchdog-removal/`. Do not publish
those snapshots or replay the existing INSTALL stages.

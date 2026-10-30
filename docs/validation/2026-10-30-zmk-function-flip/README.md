# Function/media flip validation — 2026-10-30

See [implementation and mappings](../../ZMK-FUNCTION-FLIP.md) and the aggregate
[result](result.json). Installed source is signed commit
`e4e9c7bef4af8c1fa28bbe9adf582a88376935a3`.

One frozen LEFT-only case qualified both previous applications and residents,
sent INSTALL once, received its acknowledgment and independently observed the
exact new image. LEFT returned automatically. No physical reconnect, RIGHT
write, right-only connection or recovery pads were needed. No INSTALL replay,
reset experiment or further firmware write occurred.

LEFT is the normal Studio application, 79,612 bytes/CRC32 `799253422`, SHA256
`e20a01fd51ff53dbf1b7280e050d7b2c571c933308a618a7e14e7865ed5b8fa1`.
RIGHT remains 43,752/2368050678; both resident CRCs remain `1770733249`.
The baseline, post-install and final post-layout both-half health windows each
lasted 70.375 seconds. All had zero transport/relay retries, reconnects and
watchdog feeds, with exact identities before and after each window.

The firmware update preserved all 288 saved bindings and four layer names/order
against a fresh clean snapshot. Selective import then restored exactly the one
function/media flip position. Save/reload and a new serial connection verified
that binding against runtime metadata and preserved every other 287 binding,
layer identity/name/order and physical geometry, with no unsaved changes. The
private Vial source and factory keymap are unchanged. Normal local Studio is
connected and displays all four saved layers. Read-only battery telemetry
remains fresh at 100%.

Flip starts off. The parameterless action is advertised through actual Studio
metadata, but mechanical flip actuation and resulting host/media actions were
not observed during this run. Enabled flip persistence across an actual hardware
restart was not exercised. Native tests cover every stock mapping, release
context across flip/OS changes, duplicate presses, local brightness actions,
strict load and deferred/coalesced/retried settings saves. The real backend
fixture rejects a queued runtime write when USB disappears before dispatch,
while retaining normal pre-enumeration startup writes. No hardware verification
of the enabled mode or live status widget is implied.

`make test test-zmk check-tools` passes: 88 repository, 22 migration, 25 ZMK
Python, 16 battery diagnostics and 177 updater tests, with one optional
built-resident integration skip, plus native C/sanitizer suites. Ruff, strict
mypy and edited C formatting pass. The central, peripheral, radio qualification
and update-test ARM builds pass; only the normal central was installed.
Static central RAM is 27,576/27,648 bytes with all existing stacks retained.
Compiler frame checks and actual linked wrapper/worker calls were inspected;
hardware stack watermarks remain unmeasured. Existing upstream KSCAN deprecation
and RWX linker warnings remain.

A fresh independent review found two important issues: the discontinuous HID
number-row range and USB loss between save enqueue/dispatch. Explicit HID-gap
regressions and an execution-time shared backend gate fixed them with failing
then passing tests before the final suites, builds and installation.

Conversion supports 284 of 288 bindings. Four positions remain unassigned:
one logo effect, one reserved custom action and two QMK settings resets. This
change adds only function/media flip; the stock white flip indicator is outside
its scope.

Private snapshots, frozen package/helpers, logs and verification scripts are
archived locally with a checked SHA256 inventory and excluded from Git. Archived
operators are provenance, not resume commands. This directory contains only
sanitized aggregates, without personal mappings, source-export hashes, host
identifiers or bond data.

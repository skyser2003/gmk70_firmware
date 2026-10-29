# Battery indicators and saved layout validation — 2026-10-29

See [implementation and installed result](../../ZMK-BATTERY-LAYOUT.md) and
the aggregate [result](result.json). Source
`74f1012c493f2bae6b2efaeffb3b235e8cfe964b` adds the Studio battery actions,
bounded UART cache, read-only telemetry and separate offline/device layout tools.

One LEFT INSTALL was acknowledged, followed by independent exact-image/resident
checks and automatic return. RIGHT was not written. Baseline, post-install and
post-layout both-half health windows were 70.172, 70.110 and 70.203 seconds, with
zero retries, reconnects or watchdog feeds. Neither physical reconnect nor
right-only recovery was needed.

The fresh original Studio map survived the firmware update exactly. The user's
selected 275 supported Vial bindings and 13 unassigned unsupported bindings were
then saved and reloaded. A new serial connection compared all 288 bindings,
runtime metadata and physical geometry, preserving layer names/order. The source
export and compiled factory defaults are unchanged. Normal Studio reconnects and
displays four layers.

Hardware telemetry reports 100%, cable/full asserted and 111 replies to 111
queries. The user confirmed both visible held indicators and lighting restoration
on release. Hardware low-charge/stale/RGB-off behavior, a full charge/discharge
cycle and thread stack watermarks remain unverified; native tests cover the
software state transitions and bounds.

`make test test-zmk check-tools` passes: 88 repository tests, 19 migration tests,
14 ZMK Python tests, native firmware fixtures, 16 battery-diagnostics tests and
138 firmware-update tests (one optional built-resident integration test skipped).
Ruff, strict mypy and edited C formatting checks pass. Studio, peripheral and
qualification ARM builds pass linked-image checks. Existing upstream KSCAN
deprecation and RWX linker warnings remain.

Private snapshots, personal import plans, device logs, frozen inputs and operator
sources are archived locally with a SHA256 inventory. They are excluded from Git;
archived operators are provenance, not resume commands. This directory contains
only aggregate validation and no personal mappings or device paths.

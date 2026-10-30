# One GMK70 ZMK image for both halves

The canonical `wired-studio-updater` package uses one identical application for
LEFT and RIGHT. The existing PB9 role pin selects the correct matrix wiring,
serial role and RGB behavior at startup. This keeps the board integration in
one maintained source tree.

LEFT remains the central: it resolves bindings from both halves, owns the saved
Studio keymap, routes host reports and controls battery/radio features. RIGHT
scans its keys, serves split transactions, displays the central's RGB frames and
retains the application updater. The shared binary does not change these roles
or add untethered split operation.

## Studio and storage ownership

Only LEFT mounts the existing Studio settings banks. RIGHT never reads, formats
or writes that application-tail region, which can still contain historical
firmware bytes. Its backend registers as unavailable; load/save calls fail
immediately without claiming storage or waiting for the updater worker.
Upstream behavior IDs can still be assigned in RAM during startup.

Studio's secured editing operations remain locked on RIGHT. USB/CDC descriptors
and idle RPC machinery are compiled into both copies, so a directly connected
RIGHT can enumerate them; it is not a replacement Studio/radio central. Connect
Studio to joined LEFT USB as before. LEFT's settings, lock-on-storage-failure,
startup behavior-ID persistence and runtime write gates remain unchanged.

## Package and installation contract

The explicit `zmk-studio-universal` profile accepts either target. Legacy
`zmk-studio` packages remain LEFT-only. Both Studio profiles retain the 104 KiB
application bound, native link address/footer, settings reservation and exact
pinned resident prerequisite. Offline image checks require the actual linked
role guard around the settings mount before producing universal role metadata.
Builds, tests and packaging never control the keyboard.

The package is `artifacts/zmk/wired-studio-updater/`. Build it with
`make build-zmk-wired-studio-updater` after loading `scripts/project.sh`.
The smaller `wired-peripheral` image remains available for compatibility and
recovery; it is not required by the universal distribution.

Each half still stores its own copy and receives its own application write.
Both can be installed through the existing joined LEFT USB connection. The
qualified installer freezes one selected target per case; `create-case` accepts
`--target left` or `--target right`, defaulting to LEFT. Device actions accept
only the frozen case. Both before identities/residents and advancing uptime
must qualify for 70 seconds. After sealing, the selected session/received count
and unchanged idle peer are checked again.

INSTALL is sent once. A durable case marker prevents replay; lost/deferred
replies permit STATUS polling and independent read-only observation. No ABORT
follows an install request. The observer verifies the exact new target,
unchanged peer and both residents, then another 70 seconds of health. The
ordinary updater shares the same one-shot dispatch logic and admits the
universal package for `--target both`; it does not add the qualified case's
extra health/continuity checks.

The initial transition installs LEFT first and verifies its saved map and
service health with the old peripheral unchanged. It then installs the same
application on RIGHT through LEFT USB. Later central-only feature changes can
still update LEFT alone when split protocol compatibility is retained.

Permanent firmware neither initializes nor feeds a hardware watchdog. Residents,
settings schema, factory keymap and feature mappings are unchanged. The shared
image retains the existing Studio stack configuration; RIGHT now carries the
same full image and RAM allocation. Physical power cycling remains a fallback
for a hung application, without a guarantee of rollback from a faulty image.

## Validation

Dedicated native tests start the peripheral with inaccessible settings memory,
check immediate rejected saves with no storage claim/write, and retain the
existing LEFT startup/save/reload/failure tests. Image tests reject missing or
incorrect role guards. Python tests exercise universal and legacy admission,
both selected targets, sealed session/peer changes, lost acknowledgments and
no replay/ABORT behavior.

## Installed result — 2026-10-30

Both halves now run the identical normal application from signed source
`bf0ccad85030f886e39aa13c0663e8c022aab654`: 79,624 bytes/CRC32 `893851802`,
package application SHA256
`1e3cfa64060fbbeb7832dd8f5545d45bdba1cd4c6c212cfb2d38bf633739a1d9`.
Both resident CRCs remain `1770733249`. LEFT then RIGHT each acknowledged one
INSTALL and returned automatically through joined LEFT USB, without a physical
reconnect or pad procedure. Both before/after case qualifications and the final
both-half health window passed with zero retries/reconnects/watchdog feeds.

All 288 saved bindings and four layer identities/names/order are preserved
exactly, with physical geometry/runtime metadata verified and no unsaved changes.
Normal Studio is connected with all four layers visible. Desktop reconnect
recovery and actual physical coverage are detailed in the
[sanitized validation](validation/2026-10-30-zmk-universal/README.md).
The user confirmed keys and RGB work on both halves, including the saved
lighting-control action. Cold-start behavior and hardware stack watermarks
remain unmeasured in this validation.

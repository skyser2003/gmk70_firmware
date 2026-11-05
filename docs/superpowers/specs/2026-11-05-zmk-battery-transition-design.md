# ZMK battery transition diagnostic and first correction

The user authorized trying the proposed main-firmware fix after reporting that
Bluetooth -> wired seeds 100%, returning to Bluetooth retains it with or without
USB power, and full off/on restores normal. The initial voltage/full-mode trigger
is not yet captured. Implement a correction for the confirmed exposure to brief
full-input pulses, and a bounded trace to distinguish it from the USB sampler
recovery hypothesis. This is a candidate until the physical sequence passes.

## Approaches

Use the existing single UART owner and native ZMK sensor integration. Require
1,000 ms of continuously asserted cable/full inputs before publishing full or
sending `A6 66`; clear qualification immediately on either input falling. This
prevents short pulses from seeding a retained full estimate. Add a cache-only
read-only trace alongside the compatible existing battery getter.

Blind removal of USB recovery can recreate the previously demonstrated sampler
freeze. Replacing the curve without voltage data cannot establish real charge.
Radio firmware changes are a separate deployment and are not part of this
candidate. Preserve the current 5.5-second recovery while collecting evidence.

## Interfaces and data

Keep namespace `0x72`, operation `0x21`, unchanged. Add operation `0x23`, with
the same LEFT USB/wired gates, nonzero nonce and zero request payload. Return
magic `BTRC` and sixteen trace bytes: raw cable/full and qualified-full flags,
physical selector, last transmitted charger command, percentage preceding the
last transition to 100 (255 if unknown), that transition's uptime, saturated
16-bit full-command/raw-full-rise counts and last full-command uptime. No keys,
host identifiers, radio memory access, settings or reset actions are recorded.

Copy trace state under the same lock as the battery cache. Update charger-command
evidence only after a complete UART frame is submitted; do not count a partially
transmitted frame. Use unsigned uptime subtraction for qualification at wrap.
Preserve qualified low-battery handling and immediate unplug status.

Extend the existing strictly typed battery reader with `--trace`; normal output
and requests remain unchanged. A trace is diagnostic metadata, not proof of a
fresh physical voltage conversion or of completed charger acknowledgment.

## Constraints and acceptance

Keep wired percentage reporting, exclusive USB/radio typing, one universal file,
saved settings, the resident updater and the permanent no-watchdog policy.
Normal application flash is limited to 106,496 bytes. Check actual RAM against
28,544 bytes, preserving the existing 14,336 stack bytes. Do not remove product
features to fit the diagnostic. Builds/tests perform no device writes.

Offline acceptance covers short/full pulses, stable full, immediate fall,
startup and timer wrap, immutable partial-frame ownership, trace nonce/layout,
saturating counts and getter side-effect freedom. Run full repository and tool
checks, compile affected ZMK distributions and review the task's diff. Device
testing, when LEFT USB is available, backs up the clean saved map and qualifies
the first LEFT-only candidate before changing RIGHT. A production success claim
requires repeated Bluetooth -> USB -> Bluetooth round trips without a corrective
power cycle; unavailable USB or uncaptured voltage must remain explicit limits.

## Resource refinement

Keep `gmk_battery_status` and its existing getter small. A separate
`gmk_battery_trace_get(struct gmk_battery_trace *)` reads the trace from the same
locked cache, so LED/native sensor reads do not copy diagnostic metadata. The
canonical Studio and its update-test image expose operation `0x23`. The temporary
legacy bridge and F24 qualification image reject that new operation and omit
collection; their pre-existing features and full-input qualification remain.
This resource decision was necessary to retain all existing functionality within
the 106,496-byte limit. No trace struct is copied as wire bytes: fields are
serialized explicitly with fixed little-endian widths.

Expire the prior sample before parsing fresh replies, so a first 100% after an
unobserved freshness deadline records unknown history rather than stale data.

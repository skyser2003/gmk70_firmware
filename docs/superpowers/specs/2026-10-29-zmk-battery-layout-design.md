# Battery indicators and saved-layout migration

Implement the requested held charging/battery indicators, then import the user's
private Vial export into saved ZMK Studio settings. The compiled initial keymap
remains the factory layout. Keep the working native RGB, residents, radio firmware
and permanent no-watchdog policy unchanged. Work continues autonomously under the
existing device authorization, with tests/builds separate from device operations.

Use the maintained vendor UART protocol for battery percentage rather than
inventing a voltage calibration. A bounded, nonblocking UART3 poll runs in the
existing updater worker, sends only battery inquiries and acknowledgments, and
publishes a cache-backed Zephyr state-of-charge sensor. A7 indicates USB cable;
A15 is the stock charger-full input. Unknown/stale percentage is explicit.
Full wireless transport is unnecessary for this feature and remains out of scope.

The central owns two Studio-visible held actions: level and charging. Battery
level draws the stock ten number-row pixels green (red below 30%); unknown data
uses a distinct purple indication. Charging overlays the left Fn LED red, or
green when the full-status input is asserted with cable present. Release restores
the current animation/off state without changing persistent lighting settings.
Both actions can be held concurrently and must work with ordinary RGB off.
The existing peer RGB protocol can display these central frames, so only LEFT
needs new firmware. No right-only operation or recovery-pad procedure is planned.

The offline migration resolves physical matrix positions and translates supported
bindings into named ZMK behaviors, reporting unsupported actions explicitly.
The private export and converted layout stay outside version control. Before
application, back up the freshly saved Studio map, resolve actual behavior IDs,
write through Studio RPC, save/reload, and compare all bindings. Preserve the
source export and do not replace compiled keymap defaults. Unsupported wireless
controls require an explicit migration disposition rather than silent loss.

Verify malformed/embedded radio packets, bounded MMIO, stale/unknown data,
overlapping holds, off-state restoration, split slicing, strict conversion and
round-trip Studio persistence. Build and inspect the real ARM image before a
single guarded LEFT installation. Collect read-only health and battery telemetry
after activation, then validate visible indicators and apply the custom map.

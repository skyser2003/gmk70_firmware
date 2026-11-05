# Battery refresh and radio-fault sleep follow-up

The earlier charging-heartbeat installation did not resolve the reported freeze.
This follow-up distinguishes fresh transport replies from fresh ADC measurements.

## Investigation

- USB capture: 31 samples over 300 seconds, percentage fixed at 30%, fresh query
  replies and zero radio errors. No resets or writes during the capture.
- A brief selection of an existing Bluetooth slot yielded 49% without a power
  cycle. Read-only battery RAM confirmed internal ADC source, BAS enabled,
  charging state 3 and an active ten-second sampler. USB output was restored.
- Another USB capture held at 49%. Bluetooth wake affected measurement activity,
  rather than only the display cache.
- Captured radio image SHA256:
  `100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c`.
  Offline proof executes 35 actual SOC-mapper cases and 30 power-handler cases,
  plus repeated USB selection. ADC calls and unrelated hardware effects are
  stubbed; this is not a complete radio emulator or physical wake proof.
- USB selection at `0x10011A68` posts deep event 1; `0x10017210` stops sampling.
  Wake event 2 at `0x10017256` restarts the ten-second sampler. Complete UART
  commands reset the four-second counter at `0x100121EC`.
- The actual mapper at `0x100070D4` uses different charge/discharge curves and
  hysteresis. It cannot establish real battery capacity or current consumption.

## Installed correction

After acknowledged USB selection, hold normal UART work for 5.5 seconds, then
resume the existing one-second charger/three-second battery maintenance. Permit
frame ACKs and immediate explicit wireless selection. Reuse existing control
state; native USB typing remains independent.

Also retire failed radio work after finite attempts so it cannot permanently
block queries or ordinary/critical MCU sleep. Preserve report inhibition,
physical UART drain, parser ownership and storage/wake gates.

All eight affected wired ZMK distributions compile, including the temporary
radio qualification image. The full project tests, tool checks, targeted UART
and native-radio matrices, ARM static analysis and formatting pass. Regression
tests failed before the fixes and pass with them.

The canonical universal image is 105,972 bytes, CRC32 `1734310917`, SHA256
`e940c81a45798f74480c742efc7ea4cbd7936ea141be22cb0ed7d3d53a8c646a`.
It reserves 28,162 / 28,544 RAM bytes, including the unchanged 14,336 bytes of
stacks; 382 bytes remain unallocated. No runtime stack high-water result is
inferred from these reservations.

Both halves now verify this exact application and resident CRC32 `1770733249`.
The four before/after health windows each exceed 70 seconds with zero transport
retries, relay retries, reconnects or watchdog feeds. One INSTALL was requested
per half. RIGHT's INSTALL acknowledgment was not validated; it was not replayed.
Independent exact image/resident readback and the final health window establish
that RIGHT completed installation. No manual power cycle or right-only USB
connection was needed. No radio firmware was written.

Studio checks before installation, after LEFT and after RIGHT preserve the exact
four saved layers and 296 bindings, with clean saved state, valid native behavior
metadata and matching physical geometry.

After LEFT returned, USB telemetry read 58%. About twelve minutes later it read
73%, with continuously increasing LEFT uptime, unchanged USB destination and no
Bluetooth selection or power cycle in that interval. The initial 30%/49% plateaus
therefore differ from the installed correction's observed charging updates.
Further charging and Bluetooth-service observations are recorded in the
[hardware result](hardware-result.json).

Three uncached Windows GATT reads of the native Bluetooth Battery Service each
returned 79%, matching the radio's internal percentage with charging state 3 and
the internal ADC source enabled. USB output was then restored and seven further
samples over 60 seconds remained valid at 79%, with zero radio errors and
continuously increasing LEFT uptime.

No battery-only STOP/wake current or overnight endurance result is claimed here.
The radio-fault regression proves a software sleep blocker was removed; it does
not prove that this was the cause of the user's earlier rapid-drain report.

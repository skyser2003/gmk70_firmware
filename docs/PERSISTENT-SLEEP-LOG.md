# Persistent sleep records

The debug-updater firmware now records sleep diagnostics in external SPI flash
on each half. These records survive power loss and reset. Normal release,
release-updater and normal debug builds do not write this journal.

Install the new debug-updater application on **both halves** using the existing
compatible updater installation. The resident installer is unchanged. No EEPROM
reset, keymap migration, VIA definition reload or initial resident installation
is required to move from the preceding V15/debug-updater application.

## Capturing the fault

1. Use Bluetooth normally and wait for the failure. No PC reader needs to run.
2. Power-cycle the keyboard as needed to recover it. The saved journal remains.
3. Connect USB to the left half, link the halves, select wired mode and close VIA.
4. From the portable package directory, run:

```sh
python read_sleep_log.py --target both --output sleep-records.json
```

Python 3.12+ and `hidapi==0.15.0` are required on the OS owning USB. Ordinary WSL
does not automatically see Windows USB HID. From a configured repository:

```sh
uv run --offline --frozen --project tools/firmware-update \
  python tools/firmware-update/read_sleep_log.py --target both --output sleep-records.json
```

The reader sends only log INFO, log READ and updater STATUS requests. It does
not erase records, reset the keyboard, change settings or install firmware.
Both raw journals and decoded records are saved with CRC and SHA-256 checks.
Existing output files are never overwritten. If one target fails, the successful
target and the other target's error are still saved; the command exits with
status 1. A right half that is still unresponsive cannot answer a relayed query;
its saved journal can be read after its power/communication is restored.

Storage flags are printed for each half. `storage_ready=false` means the
flash identity/capacity check failed. `region_owned=false` means existing
unrecognized data was found in the reserved region; it was not overwritten.
`write_io_error=true` means journal writes stopped for this boot after an I/O
error. These conditions are reported instead of assuming that a record was
saved. No automatic settings reset or chip erase is attempted.

The firmware accepts the observed Zbit `5E3213` ID (ZB25D40B, 512 KB).
Earlier bundles reject it because manufacturer `5E` and type `32` were missing
from the logger's allowlist; a successful MCU application update does not imply
that external-flash logging is available. Install the corrected application on
both halves, then check `storage_ready=true`, `region_owned=true` and
`write_io_error=false`. Other unrecognized Zbit IDs still fail the check.
The [public flash configuration](https://github.com/9names/bflb-mcu-tool/blob/d65ac5afe5a51c82d67fef3eb834619cee402844/bflb_mcu_tool/utils/flash/bl602/ZB25D40B_5e3213.conf)
confirms the existing driver's write-enable/status/page-program/sector-erase
commands, 256-byte pages and 4 KB sectors. The capacity and journal ownership
checks remain in force; the fix does not erase existing storage or settings.

## What is saved

A checkpoint contains the latest 16 significant events, the configured sleep
timeout and selector, full accepted idle duration in milliseconds, manual-sleep
flag, sleep reason/context, radio battery percentage, local power/LED/critical
battery state, wake cause, last peer status/loop age, and RPC counters.
Ordinary key identities and typed text are not recorded.

Checkpoints are made once before STOP entry, once after actual wake restoration,
and once when a previously successful peer link starts failing. Repeated
NONE/UART STOP returns and repeated identical link failures do not write more
checkpoints. Startup scans existing records but writes none. The last 32
checkpoints fit in the journal; sector rotation discards older records.

`recording_session` groups checkpoints from the same recording boot. It is
derived from the prior generation; boots that write no checkpoint are not
counted. Generations order records across power cycles. Uptime and idle time
exclude MCU STOP time and are not wall-clock timestamps.

The default wireless idle timeout remains five minutes (selector 7, 300000 ms).
Stock's stored selector 1 chooses one minute. A forced radio-disconnected,
critical-battery or host-suspend request can bypass the ordinary idle timeout.
For example, a left record with `configured_timeout_ms=300000`,
`accepted_manual=true`, `sleep_reason="critical battery"`, `battery_percent=0`
shows a forced request despite a five-minute configured timeout. The inherited
critical-battery branch requests sleep after 60 seconds at its stop threshold;
it was not replaced by the ordinary idle deadline. The stock power follow-up
keeps that latch local and sends ordinary peer sleep. Older firmware's right
records can still show an inherited latch; the reader preserves that legacy
interpretation. Reason 4 means host suspend (radio or USB), and reason 5 means
the hidden USB special request. See [the power follow-up](STOCK-SLEEP-WAKE-MATCH.md).

The physical one-minute trigger has not yet been captured. This implementation
does not overwrite a stored one-minute selector or disable battery protection.

## Storage and power loss

The journal occupies external flash `0x10000..0x11FFF` (two 4 KB sectors).
QMK's 4 KB logical settings use an 8 KB backing store, but its erase operation
covers the entire first 64 KB block. The journal begins beyond that block; it
does not consume macro bytes or the internal firmware/updater partitions.
Compile-time geometry/overlap checks and runtime NOR JEDEC capacity checks guard
the allocation. Unknown nonblank journal contents disable writes.

Each 256-byte page has a generation/session, 56-byte header, sixteen 12-byte
event slots, CRC32 at byte 248 and commit word at byte 252. Data is programmed
and verified before the commit word is programmed. Pages with an incomplete
commit or invalid CRC are ignored. Sector rotation never erases the sector
containing the newest valid checkpoint. A shared mutex serializes whole NOR
operations with settings writes made by the split worker.

All journal I/O runs in the main thread, outside the recorder's IRQ lock. It is
not called from a wake ISR or split transport callback. Writes add execution
time; their physical latency and effect on fault reproduction need testing.
The pre-STOP record remains useful even if the MCU cannot execute another
instruction. A completely hung or unpowered MCU cannot save its final event.

## Read protocol

Updater ABI 1 is retained. Operations 10/11 are read-only extensions on the
existing `GMK_UPDATE_RPC`. Packet validation requires a nonzero correlation
token, no request payload, INFO offset/count zero, or READ count 1..16. Responses
echo the token/offset; the host polls STATUS for deferred completion and verifies
the whole journal CRC before and after reading.

INFO returns four little-endian words: size (8192, or zero if unavailable),
raw-journal CRC32, format 1, and `(JEDEC ID << 8) | status flags`. Flags are
ready=1, owned=2 and I/O error=4. READ returns only bytes inside the journal.
Queries are rejected during active firmware updates and outside wired mode.
The portable update state machine and resident installer reject these operations;
normal/release-updater firmware reports them unavailable.

Native tests execute the production journal with simulated NOR programming and
power cuts at every byte of data/commit writes, interrupted sector erase,
rotation, corrupt/unknown storage, checkpoint deduplication, exact forced-idle
metadata, bounds and read-only queries. Separate tests check the NOR adapter's
identity/capacity/locking boundary and host decoding/CRC/partial capture behavior.
These tests are not hardware validation. No device is written by builds/tests.

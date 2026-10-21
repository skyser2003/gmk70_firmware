# GMK70 Bluetooth radio capture

This optional Vial-updater build records whether radio packets reach the WB32.
It does not fix or change Bluetooth packet handling. Hardware testing is pending.

The inspected GMK70-2 identifies its Bluetooth manufacturer as `Freqchip` and
exposes usage page `0xFF00`, usage `0`, report ID `3`, with 31 payload bytes in
each direction. A VIA protocol-version probe wrote 32 bytes including the ID,
then timed out. The maintained parser accepts only 32-byte RAW payloads. These
facts do not establish whether the radio forwarded that particular request.

## What is recorded

RAM counters track UART bytes, ignored leading bytes, ACKs, valid frames,
checksum failures, RAW headers/oversized lengths, valid RAW lengths 31/32/other,
actual RAW callback dispatches, and outbound RAW requests. Separate Bluetooth
counters survive a switch to USB. A cached module-version reply includes an
explicit received flag: zero without a reply is unknown, not firmware version 0.
The version byte is a vendor value; no major/minor interpretation is inferred.

No key identities, typed text, packet payloads or persistent logs are stored.
There is no capture-reset command. Power loss or MCU reset clears the capture.
Checksums and packet dispatch retain their normal behavior. Counter hooks add
small RAM/CPU overhead in this temporary build; they compile out of normal builds.

## Build and package

```sh
source scripts/project.sh
make build-vial-radio-diagnostic
```

Outputs are isolated under `artifacts/vial/vial-radio-diagnostic-updater/` and
`.work/vial/vial/radio-diagnostic-updater/`. The portable package is
`gmk70-vial-radio-diagnostic-updater.zip`. It includes the firmware, resident,
manifest, updater and `read_radio.py`. Normal release assets are not replaced.
Resident ABI 1 and the application address `0x08002000` are unchanged.

## Install

Export your Vial configuration first. This diagnostic build retains settings and
the installed build's EEPROM magic when the native core, GMK70 schema
`0x564D4702` and non-erased VIA magic are valid. It runs the same board/layout
startup hooks. Invalid or stock layouts still follow native initialization.
Normal Vial releases retain their original per-build magic behavior. Returning
to the exact previously installed image retains its original magic; installing
a different normal build can still reset settings. No raw-flash migration or
cross-schema import is attempted.

Use the existing [split updater](https://github.com/skyser2003/gmk70_firmware/blob/main/docs/SPLIT-UPDATER.md). Connect the left USB half
in wired mode and keep the right powered and linked. Unlock in Vial with
**Escape + left Shift**, then close Vial. These explicit commands inspect and install:

```powershell
uv run --no-project --isolated --python cpython-3.12-windows-x86_64-none --no-build --with hidapi==0.15.0 python update.py . --target both --inspect
uv run --no-project --isolated --python cpython-3.12-windows-x86_64-none --no-build --with hidapi==0.15.0 python update.py . --target both --install
```

Use `application.bin` through the resident updater. `install.bin/.hex` is for
initial direct installation, not an application update. The updater checks
resident compatibility and updates/verifies the right half before the left.
Builds and tests never perform these device operations.

## Capture without losing power

Use native Windows PowerShell, not WSL. Keep a USB cable connected throughout
the test for power; select the connection with the keyboard's mode switch.
Close Vial and other HID readers. The diagnostic USB getter needs no unlock.

1. In wired mode, save the baseline:

   ```powershell
   uv run --no-project --isolated --python cpython-3.12-windows-x86_64-none --no-build --with hidapi==0.15.0 python read_radio.py --output radio-before.json
   ```

2. Switch to Bluetooth and confirm typing works. Repeat the same report-ID-3
   VIA protocol-version probe once. It is included below. Leave the USB cable
   attached for power, and disconnect the 2.4 GHz receiver.
3. Return to wired mode without switching keyboard power off, unplugging its
   power, flashing, resetting, or invoking a bootloader.
4. Save the post-probe snapshot:

   ```powershell
   uv run --no-project --isolated --python cpython-3.12-windows-x86_64-none --no-build --with hidapi==0.15.0 python read_radio.py --baseline radio-before.json --output radio-after.json
   ```

Share `radio-after.json` and the probe output. The reader uses only five
payload-free USB getters, checks their token/page/protocol, freezes one snapshot
across pages, and refuses to overwrite an existing output file. It never sends
reset, settings-write, unlock, bootloader, or firmware-update operations.
Uptime/counter decreases mark the comparison suspect; they cannot prove that
every power cycle would be detectable. RAM history is lost on power loss.

Bluetooth probe, unchanged from the original test:

```powershell
uv run --no-project --isolated --python cpython-3.12-windows-x86_64-none --no-build --with hidapi==0.15.0 python -c "import hid; d=next(x for x in hid.enumerate() if x.get('bus_type')==2 and (x.get('product_string') or '').startswith('GMK70') and x.get('usage_page')==0xff00 and x.get('usage')==0); h=hid.device(); h.open_path(d['path']); q=bytes([3,1])+bytes(30); print('Sent:',h.write(q)); r=bytes(h.read(64,2000)); print('Received:',len(r),r.hex(' ') if r else 'TIMEOUT'); h.close()"
```

## Interpretation

- Bluetooth `raw_31` increases: a checksum-valid 31-byte RAW payload reached the
  MCU. The unchanged parser rejects that length before the VIA/Vial callback.
- `raw_32` and `raw_dispatched` increase: requests reached QMK. If
  `raw_tx_requests` also increases, QMK requested a radio reply; that does not
  prove Bluetooth delivery to Windows.
- UART activity increases but `raw_headers` does not: the radio link was active
  without an observed `AF 60 length` header. Radio-side forwarding/framing needs
  further investigation.
- Only headers or checksum failures increase: framing or corruption needs
  investigation; the firmware has not established a valid configuration frame.
- No Bluetooth UART activity: validate that Bluetooth was selected, working and
  continuously powered before drawing conclusions about RAW forwarding.

## USB capture protocol

The separate optional command is `22 52 44 01 page 00 token_lo token_hi`, padded
to 32 bytes. It is available only at an active wired USB left master. Responses
echo the header/token, with status in byte 5 and six little-endian u32 values at
byte 8. Status is 0 OK, 1 unavailable, 2 bad page, 3 bad ABI, 4 stale snapshot.
Page 0 freezes metadata and counters. Pages 1/2 are total counters; 3/4 are
Bluetooth counters, in the order listed above. Metadata is uptime, version-reply
received, version byte, last BT device, maximum RAW header length and group count
(2). Switching modes does not clear RAM. The split updater protocol is unchanged.

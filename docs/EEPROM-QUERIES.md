# EEPROM backups through the left USB connection

The updater builds can read either half's logical EEPROM through the existing
split cable. Only the left half needs USB. The right half returns its own stored
bytes; the bridge does not substitute the left half's settings.

This feature requires the EEPROM-query application on both halves: debug updater
on the left, and debug or release updater on the right. Ordinary release/debug
and the supplied stock firmware do not implement this extension. No new split
transaction ID or resident installer is needed. Existing firmware-update commands
and installer ABI remain unchanged, so an already installed compatible updater
can relay the new application using the [existing update procedure](SPLIT-UPDATER.md).
Build the applications with `make build-updater`.

## Fetch a backup

Close VIA and other HID readers, connect the halves, plug USB into the left half,
and select wired mode. Run from the repository root:

```sh
bash scripts/read-eeprom.sh --target right --output right-eeprom.json
bash scripts/read-eeprom.sh --target both --output both-eeprom.json
```

The default target is `right`; `left` is also supported. Output must be a new
file. The script loads the project environment and uses the updater's locked
Python/HID project. On Windows, run its reader directly with an appropriate
Python environment, or select the Windows Python executable from WSL using
`GMK70_DEVICE_PYTHON`, as described in the updater instructions:

```sh
python tools/firmware-update/read_eeprom.py --target both --output both-eeprom.json
```

The reader sends only EEPROM INFO, EEPROM READ and the existing STATUS getter.
It does not stage firmware, change settings, reset the keyboard or enter DFU.
Builds and tests do not invoke this script or enumerate devices.

## Saved information

The JSON file contains a timestamp and separate `halves.right`/`halves.left`
records. Each record includes:

- All 4,096 logical EEPROM bytes as `logical_eeprom_hex`, CRC32 and SHA-256.
- Logical size, query protocol and storage layout version.
- For the current V15 layout, decoded core/VIA markers, keymap configuration,
  radio selections, stored idle-sleep timeout, board flags, RGBLight and RGB
  Matrix settings.
- All four 10×9 mapping layers, four encoder assignments and the entire
  3,322-byte macro buffer, including its final validity byte.

Mappings and encoder values preserve the exact stored 16-bit keycodes and their
big-endian EEPROM order. Macros are retained as bytes, including QMK control
sequences. See the [storage layout](STOCK-V15-RECONSTRUCTION.md#logical-eeprom-layout).
An unknown future layout is saved as raw bytes without a speculative decode.
Stored values can differ from current runtime settings while firmware has a
deferred save pending; for example, temporary brightness changes need not yet
appear in EEPROM. Differences between halves are reported as an equality check
when reading `both`; the two captures are taken separately.

This is a logical settings backup, not a physical SPI-flash/wear-leveling dump
or a VIA save-layout file. No restore operation is provided. Do not assume its
raw bytes can be restored into a different EEPROM layout.

## Consistency and scheduling

INFO reports the logical size, layout and CRC32. The reader fetches 16-byte
chunks, requests INFO again, and accepts the capture only if both metadata
records agree and the assembled bytes have that CRC32. It retries an inconsistent
capture up to three times, then fails without producing a backup. Close tools
that change settings while reading.

Requests are queued by the USB/split callback and return BUSY immediately. QMK's
normal housekeeping task reads the EEPROM cache and publishes the result; STATUS
polls retrieve completion. A nonzero token and echoed offset identify each query,
so stale replies cannot silently replace another chunk. EEPROM reads and CRC
work do not run under the callback's system lock, and no second 4 KB buffer is
allocated in firmware. There are no background reads, CRC scans or extra idle
split transactions. Queries during an active firmware-update session are rejected
without extending that session's timeout.

## Wire extension

These operations reuse updater command `0x7F`, ABI 1 and `GMK_UPDATE_RPC`.
All integers below are unsigned little-endian; EEPROM payload bytes are copied
without transformation.

| Request bytes | Meaning |
| --- | --- |
| 0–2 | `7F 70 01` |
| 3 | Target: 0 left, 1 right |
| 4 | Operation: 8 EEPROM INFO, 9 EEPROM READ |
| 5 | INFO: 0; READ: requested count 1–16 |
| 6–7 | Zero |
| 8–11 | Nonzero correlation token |
| 12–15 | INFO: 0; READ: logical byte offset |
| 16–31 | Zero |

Completed replies echo bytes 0–4, token and offset. Byte 5 uses existing updater
result codes; byte 6 is zero for INFO or the returned READ count, and byte 7 uses
existing role bits (left/master = 3, right/slave = 0). INFO carries logical size
at 16, CRC32 at 20, query protocol 1 at 24, and layout 15 at 28. READ carries
the requested bytes at 16 and zero padding through byte 31. Out-of-range reads,
zero tokens, nonzero reserved bytes or request payloads are rejected.

Use the existing STATUS operation (1, no payload or offset) while BUSY. Its
response identifies the pending or last completed operation rather than echoing
STATUS itself. An older right receiver rejects operations 8/9; an older left
bridge can return UNAVAILABLE before relaying them. The reader explains that
both applications need query support; it never tries a write/reset fallback.

Native tests exercise the actual USB bridge, registered peer handler, EEPROM
query task, all logical storage bytes, bounds, CRC and transport/role restrictions.
Host tests model stale/lost responses, concurrent settings changes, corrupt
chunks, older firmware and output protection. Physical query/backup validation
on a connected keyboard is still required.

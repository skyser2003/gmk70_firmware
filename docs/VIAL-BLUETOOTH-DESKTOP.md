# Windows desktop Vial Bluetooth investigation

On 2026-10-23, the installed desktop Vial HID library and its original `hid_send`
function successfully read native Vial identity over the existing RAM Bluetooth
HID service: protocol **6**, UID **B691D7277FC88251**, 32-byte reply. This proves
that the desktop transport can exchange a native command. The unchanged desktop
GUI still reports no devices because its discovery requires a serial marker
that this Windows BLE HID driver does not return.

The working RAM trial was recorded in commit `a7f58c3`. Its initial checkpoint
was kept local; subsequent publication is separate from this investigation.
This subsequent investigation made no radio RAM or flash writes, changed no
Vial client files, installed no driver, and introduced no host transport bridge.
The existing [RAM trial and its reconnect limitations](../firmware/qmk-common/radio/ram-vial-trial/README.md)
remain the firmware baseline. Official Web Vial loaded the keymap and native
extension tabs during that trial; desktop GUI native discovery remains blocked.

## Evidence from this PC

| Boundary | Observation |
| --- | --- |
| Windows HID enumeration | The RAM service exposes the required usage page `FF60`, usage `61`; serial, manufacturer, and product strings are empty. |
| Installed discovery function | Seven controlled metadata cases pass: the exact serial marker permits native discovery; empty/MAC serials reject it; manufacturer/product markers do not substitute. |
| Direct Windows metadata APIs | `HidD_GetSerialNumberString`, `HidD_GetManufacturerString`, and `HidD_GetProductString` all return TRUE, last error zero, empty strings. This is not an observed error return. |
| Installed desktop transport | Original installed HID extension plus original `hid_send` returns protocol 6 and the expected UID after one physical wake key. |
| Idle attempt | The earlier getter was forwarded by the radio but received no main-MCU reply. The awake retry succeeded. This does not measure an idle timeout or establish reliable wake behavior. |
| Unchanged desktop GUI | No devices detected. The successful getter was a diagnostic invocation of original installed code, not GUI discovery or a full desktop session. |

The discovery test executes the installed Python 3.6 code objects against
controlled metadata. The transport probe opens the actual BLE HID path using
the installed native extension and sends only `FE00 GET_KEYBOARD_ID`; no
configuration setter is used. Test doubles never supply metadata to the running
desktop application. Private reports retain paths and raw enumeration details
outside maintained sources and shareable evidence.

Installed components, checked unchanged:

| Component | SHA256 |
| --- | --- |
| `Vial.exe` | `a5c595c6404bef95eaae141b6ef7bd99d4367558319ed6e2365ac790092a5d86` |
| `hid.cp36-win_amd64.pyd` | `6164658c74aa486ed2fe34e5d4be5ab06ff83dbb821bfa2e1d169fc477828374` |

## Why a firmware DIS serial does not fix this discovery path

The installed system reports Windows build **26300**. Its `hidbthle.inf` selects
`Microsoft.Bluetooth.Profiles.HidOverGatt.dll`, SHA256
`09648a6511eea778b3b72a149ddbc585422bed55ab35ac19769c6f065bbd61ad`.
The matching Microsoft public PDB identifies the IOCTL dispatcher as
`Microsoft::Bluetooth::Profiles::HidOverGatt::HogpIo::HidOverGattIoDeviceControl`.

In this exact binary, the string-request branch completes successfully without
retrieving a string, accessing the request payload/output buffer, or calling
GATT. The public trace-event symbol `_HIDOVERGATTGetStringOrGetIndexedString`
also identifies this branch. A radio Device Information Service (DIS) serial
characteristic cannot influence a host branch that never asks for it.

Six offline x64 Unicorn cases execute the actual dispatcher bytes with tracing
disabled and only WDF completion stubbed. Both string IOCTLs take this branch
and complete with status zero; synthetic request/output buffers remain untouched
and no other external call occurs. These are bounded code-path checks, not
execution of a modified driver in Windows. The independent live HidD results
above corroborate their relevance to this device.

| Reproduction identifier | Value |
| --- | --- |
| PDB GUID / age | `d8dedb1d-c183-4e88-1ff9-e2d62c70feab` / `1` |
| PDB SHA256 | `62130fdd03d986b1ea8cb6d0cdda2e0856719a37fee9c61a02f2429fa7412343` |
| Dispatcher RVA range | `[0x1F0C8, 0x1F580)` |
| String branch RVA | `0x1F457` |
| String / indexed-string IOCTL | `0xB0013` / `0xB01E2` |

Microsoft's HID documentation describes the string request as the route for
manufacturer, product, and serial strings. A Microsoft employee separately
explains the BLE HID stack limitation and using Bluetooth APIs to read DIS.
That answer dates from 2021; the current-PC binary analysis and live API results
provide the evidence for this particular installation. This finding does not
establish behavior on every operating system or future Windows release.

## Desktop alternatives and current boundary

Vial's built-in **Sideload VIA JSON** path bypasses the serial filter for a
matching VID/PID. Its layout override also skips native Vial identification and
leaves the native protocol unset. It is a possible basic VIA fallback, not a
route to native Vial extensions. Source inspection established this distinction;
a usable sideloaded desktop GUI session has not been demonstrated. No installed
client code or VIA database was changed to force discovery.

Changing the device name, manufacturer string, report-map string index, or DIS
serial cannot satisfy the native filter through the analyzed Windows string
path. A different desktop discovery mechanism or a host intermediary would
cross the user's unchanged-client or firmware-only boundary. Neither was
implemented. Full native desktop support on this PC is therefore unresolved
under the current constraints; there is no justified additional radio patch for
this discovery blocker.

The firmware-only working result remains the volatile Web Vial trial. Automatic
bonded subscription restoration, idle wake reliability, configuration setters,
and persistent deployment/recovery remain separate unfinished work. The last
full 512 KiB flash comparison after the working Web trial matched the private
stock backup; this desktop investigation did not recapture flash or send flash
commands.

## References and shareable evidence

- [Vial discovery and HID transport, upstream reference at `e42622c352b69eff523a467718db487ab9c9cbf2`](https://github.com/vial-kb/vial-gui/blob/e42622c352b69eff523a467718db487ab9c9cbf2/src/main/python/util.py).
  Installed bytecode was checked independently; a current upstream reference
  alone does not identify the installed executable's source revision.
- [Microsoft: IOCTL_HID_GET_STRING](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidport/ni-hidport-ioctl_hid_get_string).
- [Microsoft employee: BLE HID string limitation](https://learn.microsoft.com/en-us/answers/questions/401236/hidd-getproductstring-with-ble-hid-device).
- [Matching Microsoft public symbols](https://msdl.microsoft.com/download/symbols/Microsoft.Bluetooth.Profiles.HidOverGatt.pdb/D8DEDB1DC1834E881FF9E2D62C70FEAB1/Microsoft.Bluetooth.Profiles.HidOverGatt.pdb).
- Local shareable evidence: `artifacts/analysis/desktop-vial-2026-10-23/gmk70-desktop-vial-20261023-analysis.zip`.
  Includes sanitized results, driver/PDB provenance and the offline fixture;
  excludes binaries, installed client bytecode, Bluetooth addresses, HID paths,
  raw flash/RAM, ownership nonces, settings and private screenshots.

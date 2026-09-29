# Official GMK70 V13 versus V15

Compared on September 27, 2026, using the two local vendor packages. V15 changes wireless HID report routing and lighting/power handling. The original VIA JSON, compiled default keymap, battery keycodes, and examined battery drawing logic remain unchanged. There is no included changelog, so specific vendor bug-fix claims cannot be established.

This was offline analysis only. Nothing was flashed, no connected keyboard was queried or modified, and the bundled executable was not run. Existing project firmware sources and the pending battery diagnostic work were preserved.

## Inputs and package contents

| Item | Previous package | New package |
| --- | --- | --- |
| Folder under `D:\Windows\Downloads` | `ZUOYA+GMK70++QMK固件升级20241216` | `ZUOYA+GMK70++QMK固件升级20260819` |
| Firmware | `ZUOYA GMK70 V13.hex` | `ZUOYA GMK70_via_v15.hex` |
| HEX file bytes | 225,020 | 225,703 |
| Decoded application bytes | 79,976 | 80,220 (+244; about 0.31%) |
| Embedded build string | `2024-12-16-10:54:22` | `2026-08-19-16:04:24` |
| Flash interval, end excluded | `08000000..08013868` | `08000000..0801395C` |
| USB VID:PID | `342D:E491` | Same |
| USB device release field | `0x000D` | `0x000F` |
| VIA protocol version | 12 | 12 |
| Radio-library version string | `bts lib version: v2.8.2` | Same |

The build strings have no timezone. Extraction timestamps were not used as release evidence. The extra third-party Vial BIN in the older download folder is **not** the V13 comparison baseline.

Both packages contain one HEX, the VIA JSON, a text instruction file, and `qmk_toolbox_beta.exe`. Neither supplies application source, symbols, ELF/MAP files, build configuration, or a separate radio/receiver firmware image. The application remains consistent with the existing WB32FQ95/Cortex-M3 target: ARM Thumb code, the same initial stack/reset vectors (`20000400`, `080001B9`), and a ROM bootloader-entry reference to `1FFFE000`. The unchanged instruction text's ATmega32U4 toolbox selection is not evidence that this is AVR firmware.

The new JSON is byte-for-byte identical to both the V13 package JSON and the preserved repository definition. The toolbox is also byte-identical. The new 713-byte instructions are the same Chinese procedure; the old 1,692-byte file additionally contains an English translation. There are no release notes or new recovery instructions.

## Confirmed unchanged battery behavior

Custom(27) remains `0x7E1B`, and Custom(7) remains `0x7E07`. V15 has the same examined handler instruction sequences after accounting for relocated addresses:

| Function or state | V13 | V15 |
| --- | --- | --- |
| Custom-key jump table | `08005366` | `08005476` |
| Battery-level handler | `08005B96` | `08005CAC` |
| Charging-status handler | `08005BCE` | `08005CE4` |
| Battery request RAM byte | `20001494` | `200014C4` |
| Charging request RAM byte | `20001493` | `200014C3` |
| RGB indicator callback | `08004E7C` | `08004F34` |
| Cached battery percentage | `20004270` | `200042A0` |
| Split RPC function | `080084D0` | `08008730` |

The battery handler retains its device-mode guard: device value zero (USB) bypasses the request. Press/release sets/clears the local flag and sends split commands A4/A5. Charging status retains F0/F1. Both versions use transaction 15 with five-byte request/response buffers. The receiving half still has corresponding flag-setting handlers; in V15, A4/A5 dispatch to `0800536A`/`08005372`, and F0/F1 to `080053BA`/`08005298`.

The examined indicator callback retains the same ten-segment bar, red/green thresholds and charging-state branches. Its LED table is identical: `5,6,7,8,9,39,40,41,42,43`. The 720-byte default keymap (four layers, 10x9 cells, 16-bit codes) is also identical: V13 `080126C0`, V15 `080127A4`. Loading the new JSON therefore does not supply new battery assignments or fix existing saved mappings.

The physical connection selector also retains the examined logic: C15 low/C14 high is Bluetooth; C14 low/C15 high is 2.4 GHz; both high is USB. The function moves from `08002404` to `0800277C` and still reads GPIOC at `40000800`.

These findings do not establish that V15 fixes the user's missing battery display. Changes elsewhere can affect whether these routines run or whether LEDs receive power, but the direct battery feature was not rewritten. The current project's missing cross-half battery request propagation remains relevant: both official versions contain it. The live left-half failure remains undiagnosed.

## Meaningful changes found

### Wireless key reporting

V13 routes wireless individual key/modifier events through vendor-specific handlers, including `08011EA0`, with overrides around `08002CA4` and `08002D1C..08002FD8`. V15 removes that particular per-key path and adds a report-based wireless host-driver implementation:

- `08002148`: mouse report encoding, packet command A8.
- `080021A4`: consumer/system report encoding, packet commands A3/A4.
- `0800221C`: NKRO processing with six ordinary key slots and overflow bits.
- `080023E8`: keyboard report encoding, packet command A1, or delegation to the NKRO path.
- `08002434`: selects between USB and wireless host-driver tables, including sending an empty keyboard report during a switch.

Here A4 is a **radio packet** command, separate from the A4 split battery command. The packet construction and host-driver switching are structurally similar to the public wireless integration maintained in this repository; that similarity does not prove an exact source revision match.

V15 also removes V13's unconditional startup sequence at `08007704..08007712` that sets the keyboard configuration's NKRO bit and writes it to EEPROM. This concerns startup behavior; it is not proof that every factory-reset/default path changed its NKRO setting.

These are real implementation changes despite the small net size increase. They could affect wireless modifiers, shortcuts, macros, and simultaneous key reporting. The package does not identify which symptoms were fixed, and none were hardware-tested here.

### Lighting, startup, sleep and wake

- The board configuration/LED setup routine at V13 `08004ABC` / V15 `08004B50` no longer unconditionally raises A8/A5 early. V15 checks RGB enable state at `08004B96`, then drives both LED-power outputs high or low accordingly. Both images call this routine from their reset path; V15 additionally calls it during the main keyboard initialization sequence at `0800795C`.
- The sleep path changes around V13 `08002A7A` / V15 `08002DFA`. V15 explicitly reads the RGB enabled state, saves it in RAM, then disables the matrix. This replaces the older state-query sequence.
- In the slave split dispatcher, reset command DD and RGB-enable command C1 now configure A8/A5 as outputs and raise them before enabling RGB. See V15 `080052E8..08005312` and `08005382..080053AE`. V13's C1 path at `08005296` enabled RGB and fell through into an additional board helper; V15's path now returns after enabling RGB.

These changes directly concern lighting power and state restoration on the other half. Their presence supports a power/lighting maintenance interpretation, but cannot establish a particular observed bug fix without testing the real keyboard.

## Limits and reproducibility

An exact QMK release, source commit, compiler invocation, or complete build-flag set cannot be recovered from these stripped HEX images. VIA protocol 12 is not a QMK version. The unchanged `bts` version string does not prove every linked radio-library byte is unchanged; some linked routines were removed or replaced. The package offers no new source release or independently buildable radio library.

Local evidence is saved under `artifacts/analysis/official-v13-v15-20260927/`: decoded binaries, complete Thumb disassemblies, strings/descriptors, raw aligned diff, package hashes, and the temporary analysis project's source/tests. The raw diff includes relocated code and pointer tables and is not a semantic change percentage. Linear disassembly also decodes literal pools and jump tables as apparent instructions; those regions were treated as data during review. The system `file` utility falsely labels these raw vector bytes as a TTComp archive, so architecture identification uses the vectors and valid Thumb code instead.

Intel HEX records were checked for length/checksum, overlaps, gaps, and EOF. V15 decoding independently matched GNU ARM `objcopy` byte-for-byte. Fourteen parser/evidence tests passed, as did Ruff formatting/lint and strict mypy for the temporary Python helpers. Address-normalized instruction comparisons were supplemented with inspection of call destinations, literal data and dispatch tables; they do not prove whole-firmware equivalence. No firmware source was edited for this comparison, so no firmware rebuild was required.

### SHA-256 identities

| File | SHA-256 |
| --- | --- |
| V13 HEX | `5924d7516155aa499fdc65ea5168b9d7e62bd947cb8e31f668362224836d026e` |
| V15 HEX | `67818d381cb740721c10dd9d17809da603d729c974aa2017dc9e99ba8a8e9ce0` |
| V13 decoded BIN | `857a669c1fc739cd18ccd9a5526e80db10b150fbdea344b36031105e72e661f3` |
| V15 decoded BIN | `3048ee6390a42f6c81a28df3dc9f032a926fe843d904ea9154a0554dae2af0b0` |
| Both original VIA JSON files | `e878d51b9c1bdbf182feb48fa892ad822c9b4b8ff94f42ebe6b5e7fd0ba94789` |
| Both toolbox EXEs | `e373b643d42c843ec29dc1a0e2090d5aeee21addd10259b16951b32e8c9d2fd3` |

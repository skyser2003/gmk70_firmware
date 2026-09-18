# GMK70 normal QMK/VIA firmware

A buildable ZUOYA GMK70 keyboard MCU firmware based on [uk-taniyama/qmk_zuoya_gmk70](https://github.com/uk-taniyama/qmk_zuoya_gmk70), with the shared radio interface from [hangshengkeji/qmk_firmware, tri-mode](https://github.com/hangshengkeji/qmk_firmware/tree/tri-mode).

**Status: build candidate; no hardware validation or flashing performed.** USB, split matrix, Bluetooth/2.4 GHz radio control, encoder, RGB, SPI-flash settings, and VIA are compiled in. This is normal QMK/VIA, with no Vial support yet. See [hardware acceptance tests](docs/HARDWARE-VALIDATION.md) before calling it fully functioning.

## Rebuild

Use Linux or WSL2 with Git, make, rsync, tar, and Docker. The host regression tests also need GCC with address/undefined-behavior sanitizers. No local ARM toolchain is needed.

```sh
git submodule update --init --recursive
make test
make build          # normal QMK with VIA
make build-default  # same stock-style keymap without VIA
```

On this Windows machine:

```powershell
wsl -d Ubuntu -- bash -c 'cd /mnt/f/Projects/gmk70_firmware && make test && make build && make build-default'
```

The scripts read a project `.env`, or `.envrc` if there is no `.env`, before running. `JOBS=8` is the default; set it to change parallel compilation. Docker does not receive USB devices or privileged access. The scripts only build and collect artifacts.

Outputs appear under `artifacts/via/` and `artifacts/default/`: `.bin`, Intel `.hex`, debug `.elf`, linker `.map`, compiler/linker flags, build log, checksums, source hashes, and a source-overlay archive. The archive includes new/uncommitted files; the Git patch alone does not. Artifacts are ignored by Git. `artifacts/baseline/` retains the unmodified port's initial build from this development session.

The QMK image is pinned by digest in `scripts/build.sh`, with ARM GCC 8.3.1. `SKIP_VERSION=yes` avoids timestamp and Git-status probes inside the firmware; consult `build-info.txt` and source hashes for provenance. This does not prove equality with the factory binary.

## Sources and hardware

| Component | Source / configuration |
| --- | --- |
| Starting keyboard port | uk-taniyama `8d8055bfc864b36d41bbfb0bf9cd393e4c4b6e20` |
| QMK submodule | `3d4da6de29c8635c9cd232ce456d8fec8d31921b` |
| Tri-mode reference examined | hangshengkeji `580665f77746c373ada7f79a4799f20a4f2f6c6d` |
| Keyboard target | `zuoya/gmk70` |
| MCU / bootloader | WB32FQ95, Cortex-M3 / `wb32-dfu`, ROM entry `0x1FFFE000` |
| Application memory map | Flash `0x08000000`, 128 KiB; RAM `0x20000000`, 28 KiB |
| USB identity | `342D:E491`, device version `0.3.0` |
| Split | UART1, A9/A10, 115200 baud, full duplex; B9 selects master/hand |
| Radio interface | UART3, C10/C11; shared `keyboards/linker/wireless/` C sources |
| Matrix / LEDs | 10x9 logical matrix; 72 layout positions; 77 LEDs split 36/41 |
| Settings | SPI flash with wear leveling; 8 KiB backing, 4 KiB logical |

The `wls/` directory contains GMK70-specific mode-switch, indicators, and wake hooks. `keyboards/linker/wireless/` contains host report conversion, radio packet framing, command queues, transport selection, raw HID forwarding, and WB32 low power support. Its source is already integrated into this port; the entire hangsheng QMK fork is not needed as a second build base.

This builds the keyboard-side controller, **not the firmware running inside the Bluetooth/2.4 GHz radio or USB receiver**. Their existing firmware is still required. No vendor precompiled `.a` library is linked; the ARM runtime and locally compiled QMK/ChibiOS driver archives are used. `lpwr_wb32.c` does include two inherited machine-code arrays (`pre_lp_code` and `post_lp_code`, 40 and 52 bytes). They remain unchanged because replacing their undocumented hardware sequence needs validation.

## Changes from the starting port

- Select the wireless host driver when choosing BT/2.4 GHz, before asynchronous connection completes. The original condition could leave the first wireless connection sending through USB.
- Replace source-line-number macro rewriting of `raw_hid_send` with the linker's explicit wrapping mechanism. Valid 32-byte VIA responses route to USB or radio.
- Fix wireless rollover conversion so key release across empty slots cannot leave stale keys or read past the report bitmap. Keep report IDs out of radio keyboard frames, and distinguish consumer/system reports on release.
- Keep both halves awake in USB mode. Guard master-only mode scans/RPCs and repair the sleep issues listed in the hardware document. Wireless sleep remains enabled and unverified on hardware.
- Validate stored board settings and apply battery values received over split RPC.
- Share one stock-style keymap between `default` and `via`; restore Escape/grave/number order and the Windows GUI key. Restore Mac battery display and encoder maps for the default build. The vendor lighting shortcut now operates on the enabled RGB matrix driver.
- Pin the build environment, retain analysis artifacts, and provide sanitizer-backed host tests.

## Keymap and VIA

Use [keyboards/zuoya/gmk70/via.json](keyboards/zuoya/gmk70/via.json) with the VIA build. It derives its physical layout from the local factory definition, with custom-key ordering adjusted to this source. The factory JSON labels do not match this port's custom keycodes. The firmware has four dynamic layers and encoder maps; it has no Vial UID or Vial unlock configuration.

The left Fn key activates shortcuts. With the physical selector in the appropriate wireless mode, Fn+Q/W/E selects BT1/2/3, and Fn+R selects 2.4 GHz. Hold the respective chord for 3 seconds to pair. Fn+A/S selects Windows/Mac, Fn+B shows battery level in wireless mode, and the encoder controls volume/mute. Fn+encoder press enters the bootloader. Fn+Backspace held for 3 seconds resets settings. Existing EEPROM mappings can override the compiled defaults.

The inherited `KC_BTPR` placeholder had no implementation; it is transparent in this keymap. Pairing uses the implemented long-press device shortcuts. Custom slot `MOR_5` is reserved and has no handler. The inherited personal `mine` keymap remains available for compatibility but is not the normal build target.

## Verification

`make test` compiles the actual wireless and raw-HID source with the pinned QMK report types and a small hardware fixture. It runs with AddressSanitizer and UndefinedBehaviorSanitizer for dedicated and shared USB keyboard endpoint layouts. It covers initial disconnected transport selection, slave isolation, 500 changing rollover chords, key releases, report framing, system/consumer release, and raw HID length/routing.

Successful host tests and firmware linking establish a useful starting image. Pairing, radio compatibility, charging, RGB hardware, split wake timing, and operation on a particular PCB revision remain pending physical validation. Nothing is flashed or published by the local build commands.

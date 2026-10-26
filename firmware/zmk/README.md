# ZMK port workspace

Reserved for future investigation; no working GMK70 ZMK port is claimed. `make build-zmk` intentionally exits with an explanatory error.

The [GMK70 feasibility investigation](../../docs/ZMK-FEASIBILITY.md) records the
reviewed upstream versions, missing WB32 support, existing hardware mapping,
radio options, memory/update constraints and recommended USB-first approach.

ZMK is a separate firmware stack built on Zephyr, not another VIA-style UI for the QMK binary. Board/SoC integration, GPIO and matrix configuration, split transport, power management, and the external radio interface need a supported implementation. The current WB32FQ95 controller and vendor radio do not become ZMK-compatible by converting a keymap or JSON file.

Keep future board definitions, devicetree, Kconfig, keymaps, and upstream version records here within this repository. The [multi-platform layout plan](../../docs/FIRMWARE-ARCHITECTURE.md) uses a local Zephyr module plus a pinned West dependency manifest and frozen revisions, with upstream checkouts in an ignored workspace. Generated outputs belong under `artifacts/zmk/`. Share hardware evidence from `../../docs/`; do not import a duplicate GMK70 project or copy QMK C drivers as if they were Zephyr drivers.

ZMK Studio will be a build variant of this port, using the same board and drivers. It needs physical layout metadata, settings storage, an RPC transport, unlock behavior and enough memory. Enable it on the central half of a split after the underlying hardware port works. USB and BLE Studio transport support require separate validation with this MCU and external radio.

Hardware integration reference: https://zmk.dev/docs/hardware-integration/new-board

## Offline staging fixtures

`make build-zmk-trial-fixtures` builds bare Cortex-M3 reset-return and hang
programs under `artifacts/zmk/trial-fixtures/`, linked at 0x08020000. They are
not ZMK builds or normal updater packages. The separate opt-in Vial trial
receiver and watchdog recovery design are described in
[ZMK-TRIAL-LAUNCHER.md](../../docs/ZMK-TRIAL-LAUNCHER.md). RUN is disabled until
watchdog/startup integration is implemented and proven. No build executes them.

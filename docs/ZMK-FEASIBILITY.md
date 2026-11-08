# GMK70 hardware integration references

The implemented port is described in the [ZMK build guide](../firmware/zmk/README.md)
and [provenance](../firmware/zmk/PROVENANCE.md). The hardware assignments below
come from maintained board sources and pinned vendor references.

## Existing hardware and what can be reused

These assignments come from the maintained
[board definition](../firmware/qmk-common/keyboards/zuoya/gmk70/keyboard.json),
[configuration](../firmware/qmk-common/keyboards/zuoya/gmk70/config.h),
[clock configuration](../firmware/qmk-common/keyboards/zuoya/gmk70/mcuconf.h)
and wireless drivers. They are firmware evidence, without a new electrical
measurement.

| Part | GMK70 evidence | ZMK/Zephyr route |
| --- | --- | --- |
| Main controllers | WB32FQ95 Cortex-M3, one per half; current clock 96 MHz | Reuse ARM architecture support; add WB32 SoC, clock, pin control and peripheral drivers. |
| Matrix | 5 rows per half; 8 physical left columns and 9 right columns; ROW2COL; current debounce 5 ms | GPIO matrix scanning and position transforms already exist. Requires working WB32 GPIO. |
| Encoder | Left only, B7/B6 | Existing EC11 integration after GPIO support; verify direction and detents. |
| Inter-half link | UART1, A9 TX/A10 RX, full duplex, 115200 baud | Current ZMK wired split is a good physical match once WB32 UART works. Both halves need the ZMK protocol. |
| Host radio | UART3, C10 TX/C11 RX, 115200 baud, external Freqchip module | Add an adapter for the existing vendor HID/control/status protocol. |
| RGB | WS2812 via SPIM2/B15; 36 left and 41 right; RGB byte order | Zephyr LED-strip abstraction is suitable after SPI support. Stock effects and indicator overlays need separate behavior work. |
| Settings | External SPI NOR on B3/B4/B5, C12 chip select | Zephyr SPI-NOR/settings may be usable after WB32 controller integration and flash/partition verification. |
| Power | LED rails A5/A8, USB power B1, cable detect A7, charge-full A15 | Explicit GPIO/power/USB integration; sleep and wake need a WB32 PM design. |

Full matrix pins: rows A1/A2/A3/A4/C13; left columns
C0/C1/C2/C3/A6/B10/B11/B12; right columns
B13/B14/C6/C7/C8/C9/C4/C5/B0. The extra logical left column is unwired.
Handedness is B9. B6 also participates in existing wake handling; pin ownership
must account for its encoder use.

The native port supplies WB32 clock, GPIO/EXTI, USB, flash, UART, LED and power
adapters. Cortex-M3 support alone does not supply those peripherals; WB32 is
not register-compatible with an arbitrary STM32 board.

ZMK's generic split cores are reused with runtime role selection and the board's
existing matrix/RGB carrier. Matching baud rates do not make QMK transactions
compatible with native ZMK frames. The
[updater bridge](ZMK-NATIVE-UPDATER-WIRE.md) explicitly handles mixed-stack
migration.

Studio stores settings in application-owned internal flash banks rather than
the QMK/Vial external SPI store. LEFT owns saved settings and host services.
The universal binary does not make RIGHT an interchangeable central.

Vendor-authored ChibiOS register references retain their upstream licenses.
QMK-derived local integration retains compatible copyleft terms.
This local port does not claim eligibility for upstream ZMK's clean-room
contribution process; see [licensing](LICENSING.md).

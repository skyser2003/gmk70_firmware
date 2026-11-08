# QMK/Vial battery indicators

VIA and Vial share held-position tracking with ZMK through
`battery_holds.c` in `firmware/qmk-common/keyboards/zuoya/gmk70/`.
Multiple physical bindings keep a query active until the last release; the
physical hook also clears a held query after a live mapping change. The USB
guard applies to a new level-query press, while release always clears an
accepted hold. Local and received split requests are tracked separately.

The stock A4/A5 battery and F0/F1 charging press/release packets are unchanged.
Each aggregate request transition makes one send attempt, without retry or
reconnect recovery. The existing two-second `0x55` exchange distributes the
master's percentage, using 255 for unknown. Valid percentages range from 0 to
100 and expire after ten seconds. Unknown bars are purple; missing telemetry
cannot trigger low-battery warning or shutdown.

- **Custom(27), `0x7E1B`:** hold for a shared ten-segment bar across both
  number rows in Bluetooth or 2.4 GHz mode. Below 30% the bar is red; otherwise
  green. Zero still lights one segment. The reading comes from the master's
  radio, not separate measurements of both packs.
- **Custom(7), `0x7E07`:** hold for charging status on global LED 33,
  matrix position 4,2. Cable/full inputs determine the charging indication.
- Releasing restores ordinary lighting. The common hardware hook permits held
  indicators with ordinary RGB off, without changing saved lighting settings.
  Suspend and sleep retain LED power ownership.

`stock_battery.c` owns the event adapter and rendering; the shared
`battery_holds.c` owns hold lifetime. The normal builds use pinned upstream
LED-driver behavior and contain no diagnostic recorder. The original VIA
definition, custom-keycode values and five-millisecond debounce remain unchanged.

See [compatibility safeguards](STOCK-COMPATIBILITY.md#repository-safeguards-beyond-stock-reconstruction),
[ZMK battery behavior](ZMK-BATTERY-LAYOUT.md) and
[hardware acceptance procedures](HARDWARE-VALIDATION.md).

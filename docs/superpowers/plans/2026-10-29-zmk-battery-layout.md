# Battery indicators and saved-layout implementation

Spec: [design](../specs/2026-10-29-zmk-battery-layout-design.md).

- [x] Implement/test bounded UART battery cache and native sensor in
      `firmware/zmk/battery/`, using maintained protocol and GPIO evidence.
- [x] Implement/test held overlays, Studio behavior metadata and read-only
      telemetry; wire Studio profile and existing worker without a new thread.
- [x] Implement/test a strictly typed offline Vial-to-ZMK converter in the
      existing uv migration project. Keep all personal layout data private.
- [x] Format/lint/type-check, run repository/native/tool tests, build affected
      ARM variants and independently review the final firmware/device operator.
- [x] Back up the current saved layout, install only LEFT once, verify exact
      image/resident/health/telemetry and visible battery/charging actions.
- [x] Resolve unsupported migration bindings, apply the private custom map through
      Studio settings, save/reload/compare; preserve compiled initial defaults.
- [x] Record evidence and limits without personal data; sign commits and push.

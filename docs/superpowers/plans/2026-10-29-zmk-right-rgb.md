# Restore right-half RGB

The installed peripheral disables RGB and Studio suppresses peer frames. These
migration settings explain the user's working keys but dark right LEDs. Restore
the existing native split RGB implementation: enable underglow/driver in the
peripheral and disable local-only isolation in Studio. Keep the driver, protocol,
keymap, brightness limit, settings layout and no-watchdog policy unchanged.

- [x] Change the two build profiles and correct build information.
- [x] Run the existing native lighting/DMA/protocol tests and repository tests;
      compile both affected ARM images and inspect their configs/symbols/bounds.
- [ ] Capture fresh identities, both-half health and Studio keymap. Freeze the
      validated packages and separate one-shot device operator.
- [ ] Install RIGHT through LEFT first, verify its return, then install LEFT to
      enable frame transmission. Never replay INSTALL after an uncertain ACK.
- [ ] Verify both installed identities, residents, no-watchdog health and Studio
      map. Obtain visual LED confirmation; record any remaining limits honestly.

This is a correction to the previously implemented lighting feature. Autonomous
installation is authorized by the user's request and prior device workflow.
Build/test commands remain device independent. A joined-LEFT cold cycle is the
fallback only if an activation fails; no right-only USB or pad procedure is planned.

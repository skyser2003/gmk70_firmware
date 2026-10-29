# Restore right-half RGB

The installed peripheral disables RGB and Studio suppresses peer frames. These
migration settings explain the user's working keys but dark right LEDs. Restore
the existing native split RGB implementation: enable underglow/driver in the
peripheral and disable local-only isolation in Studio. Keep the driver, protocol,
keymap, brightness limit, settings layout and no-watchdog policy unchanged.

- [x] Change the two build profiles and correct build information.
- [x] Run the existing native lighting/DMA/protocol tests and repository tests;
      compile both affected ARM images and inspect their configs/symbols/bounds.
- [x] Capture fresh identities, both-half health and Studio keymap. Freeze the
      validated packages and separate one-shot device operator.
- [x] Install RIGHT through LEFT first, verify its return, then install LEFT to
      enable frame transmission. Never replay INSTALL after an uncertain ACK.
- [x] Verify both installed identities, residents, no-watchdog health and Studio
      map. Obtain visual LED confirmation; record any remaining limits honestly.

This is a correction to the previously implemented lighting feature. Autonomous
installation is authorized by the user's request and prior device workflow.
Build/test commands remain device independent. A joined-LEFT cold cycle is the
fallback only if an activation fails; no right-only USB or pad procedure is planned.

Evidence: source `0fcf8f2`; both linked configurations and symbols checked;
independent pre-install artifact/operator review found no blockers. Fresh Studio
map captured only after the user saved their newer edits. Baseline both-half
health passed 70.172 seconds with no retries/reconnects. RIGHT INSTALL acknowledged
and exact new RIGHT/old LEFT identities returned automatically; its continuous
health passed 70.109 seconds. LEFT then installed and returned automatically;
final both-half health passed 70.234 seconds. No reconnect was required. Studio
compared equal to the fresh saved map; normal Studio reopened. The user confirmed right
RGB works and Fn+X controls both halves together. All acceptance checks are complete.

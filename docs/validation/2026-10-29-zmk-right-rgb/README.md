# Right-half RGB validation — 2026-10-29

See [implementation and result](../../ZMK-RIGHT-RGB.md) and the sanitized
[result](result.json). Source `0fcf8f2` fixes the migration profiles that disabled
RIGHT RGB and LEFT peer frames. Both exact new images installed and returned
automatically through joined LEFT USB. No reconnect or recovery pads were needed.
Baseline, post-RIGHT and final post-LEFT both-half health passed 70 seconds each
with zero retries/reconnects and zero watchdog feeds. The user's fresh saved
four-layer map, including their newer edits, matches exactly after installation;
normal Studio reconnects. Both resident CRCs remain `1770733249`.

Visible right LEDs and Fn+X synchronization still await the user's confirmation.
The private inventory includes full map snapshots and local operator files; do
not publish it. Archived operator copies are provenance, not resume commands.

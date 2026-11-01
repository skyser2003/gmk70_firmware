GMK70 Vial firmware, built from the commit selected by this Vial tag.

Shared board idle scanning now uses row interrupts without pausing split
maintenance. Pairing holds track physical positions and cancel on live matrix
remapping, while native unlock checks remain intact. See
[custom feature parity and hardware limits](CUSTOM-FEATURE-PARITY.md).

The same shared radio recovery now discards stale native/flip state on actual
connection loss and sends empty transport reports past native cache suppression.
Same-channel wake preserves held input, and invalid both-low selector input is
rejected. See the fixed [custom behavior checklist](CUSTOM-BEHAVIOR-CHECKLIST.md).

NKRO defaults enabled for new/reset settings; a saved off choice survives
startup. Both Windows and Mac defaults include Fn+V. Report transitions clear
held flip contributions and inactive boot-fallback state. See
[NKRO behavior and radio limits](NKRO.md).

RGB power, temporary battery indicators and physical-key activity hooks now live
in the common GMK70 hardware implementation shared with VIA. Vial keeps its
native protocol, storage schema, unlock and fork-specific RGB routing.

The common board code also adopts the repository's ZMK safeguards: press-time
function/media outputs survive flip or OS changes, battery queries track each
held position, and invalid or stale readings show an unknown indication. Radio
input quarantines damaged raw-frame tails; actual host changes release outgoing
reports first. Deferred updater mutations recheck native unlock and USB/role
gates, while read-only or rejected traffic no longer extends upload timeouts.

Battery-level and charging-status queries now display while RGB is off, without
enabling the saved lighting setting. Custom-key descriptions contain only the
role and `CUSTOM(n)`; unused reserved slots have blank labels with identifying
tooltips. All existing custom-key numbers are preserved.

- Normal firmware: `gmk70-vial`.
- Split updater firmware: `gmk70-vial-updater`.
- Permanently unlocked split updater: `gmk70-vial-unlocked-updater` (`VIAL_INSECURE=yes`). Host settings and firmware writes do not require physical unlock in this image.
- Portable `.zip` packages contain firmware and instructions; full `.tar.gz` bundles also include ELF/map files, dependency pins, build information, checksums and matching `source.tar.gz`.

Verify downloads with `sha256sum -c SHA256SUMS`. The Vial definition is embedded; secure normal/updater builds use Escape + left Shift for physical unlock. Install the same build on both halves and export your settings before updating. Rebuilt Vial images reset dynamic mappings and macros. Unlocked builds retain the same native settings schema and per-build EEPROM magic behavior.

Updater `install.bin`/`.hex` is for initial direct installation on each verified 256 KB half. `application.bin` + `update.json` is for subsequent compatible resident updates through left USB. Secure Vial update writes require physical unlock, including when installing an unlocked image from secure firmware. EEPROM backups and diagnostics are excluded. Follow `docs/SPLIT-UPDATER.md`.

Build the permanently unlocked distribution with `make build-vial-unlocked-updater`;
artifacts are isolated under `artifacts/vial/vial-unlocked-updater/` and build
objects under `.work/vial/vial/unlocked-updater/`. Normal Vial release workflows
include this separately named package alongside secure normal/updater packages.
`make build-vial-unlocked-trial-updater` is a separate opt-in staging experiment
under `artifacts/vial/vial-unlocked-trial-updater/`; it is excluded from ordinary
Vial releases. Its trial RUN command remains disabled, and it uses the existing
resident installer unchanged. Neither build target performs device operations.

Complete stock equivalence and hardware validation of the latest changes remain unverified. QMK/VIA firmware is published separately with `via-vX.X.X` tags.

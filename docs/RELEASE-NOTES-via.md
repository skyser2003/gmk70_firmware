GMK70 QMK/VIA firmware, built from the commit selected by this VIA tag.

The shared VIA/Vial board now skips local GPIO sweeps while released and idle,
retaining native split maintenance and debounce. Pairing holds track physical
positions and cancel on live matrix remapping. See
[custom feature parity and hardware limits](CUSTOM-FEATURE-PARITY.md).

Actual radio loss now clears native reports and flip ownership, then forces
empty transport reports so pairing-suppressed releases cannot leave stuck keys.
Repeated status and same-channel wake preserve held input. Invalid both-low
selector input is rejected. See the fixed
[custom behavior checklist](CUSTOM-BEHAVIOR-CHECKLIST.md).

NKRO now defaults enabled for new/reset settings and remembers native toggle
choices. Fn+V toggles it in Windows and Mac defaults. Mode changes clear stale
reports, including held function-flip modifiers and inactive boot-fallback state.
See [NKRO behavior and radio limits](NKRO.md).

VIA and Vial now share the GMK70 hardware hooks. Held battery/charging indicators
can display while RGB is off, and physical LED power follows GUI and split RGB
changes. Temporary indicators leave saved lighting settings unchanged. Physical
key activity is tracked before features consume or replay events.

The shared board implementation also adopts the repository's ZMK safeguards:
function/media keys release their press-time output after flip or OS changes;
multiple battery bindings stay active until the last held position releases;
invalid or stale battery readings display as unknown. Radio input quarantines
damaged raw-frame tails, and host changes release outgoing reports first.
Deferred updater writes recheck USB, role and authorization gates; read-only or
rejected requests cannot keep an abandoned upload alive.

- Normal images: `gmk70-via-release` and `gmk70-via-debug`.
- Split updater packages: `gmk70-via-release-updater` and `gmk70-via-debug-updater`.
- Each `.tar.gz` includes firmware, ELF/map files, dependency pins, build information, checksums and matching `source.tar.gz`.

Verify downloads with `sha256sum -c SHA256SUMS`. Load the original GMK70 definition in VIA. Install the same build on both halves and back up settings first.

Updater `install.bin`/`.hex` is for initial direct installation on each verified 256 KB half. `application.bin` + `update.json` is for subsequent compatible resident updates through left USB. Follow `docs/SPLIT-UPDATER.md`; normal images are not resident updater applications.

These packages target reconstructed stock V15 behavior. Full stock equivalence and validation of the latest changes on hardware remain unverified. Vial firmware is published separately with `vial-X.X.X` tags.

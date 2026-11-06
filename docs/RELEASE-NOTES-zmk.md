GMK70 ZMK Studio firmware, built from the commit selected by this ZMK tag.

One universal application runs on both halves. The hardware role pin selects
LEFT central or RIGHT peripheral behavior. Keep the halves joined and connect
USB to LEFT for Studio and updates. Bluetooth and the 2.4 GHz dongle use the
existing stock radio; Studio configuration is available over USB only.

- `gmk70-zmk.zip`: portable application update package, resident reference,
  image validation report, instructions and checksums.
- `gmk70-zmk.tar.gz`: full bundle, also including ELF/map files, generated
  configuration/device tree, build information and matching `source.tar.gz`.
- Verify downloads with `sha256sum -c SHA256SUMS`; after extracting the ZIP,
  verify `sha256sum -c FIRMWARE-SHA256SUMS`.

This is an explicitly unlocked updater distribution: compatible host software
can edit settings and initiate firmware writes without a physical unlock
gesture. Wired mode, USB, role and storage-readiness gates still apply.

The `application.bin` and `update.json` are for compatible resident updates,
linked at 0x08002000. They are not a factory-bootloader image and must not be
written at the start of flash. The included `resident.bin` is a reference to the
exact reviewed resident, not an instruction to overwrite it. No direct-install
image is included. Back up and save Studio settings before updating. Both
halves use the same application; each half still requires its own update.

Existing native-split ZMK installations can use the normal application updater.
QMK/Vial and older ZMK installations need the separately built migration bridge
before moving to the native split updater; do not install canonical LEFT first
and assume it can relay to a legacy RIGHT. Follow
[native updater migration](https://github.com/skyser2003/gmk70_firmware/blob/main/docs/ZMK-NATIVE-UPDATER-WIRE.md)
and the repository's `docs/ZMK-UNIVERSAL.md`. Bridge, diagnostic and staging
trial images are excluded from this release family.

Features include four Windows/Mac layers, USB Studio keymap and knob-direction
editing, stock-radio Bluetooth profiles/dongle selection, shared RGB controls,
held battery/charging indicators and the application updater. Battery-level
presses request a fresh radio reading; automatic three-second radio queries run
only in wireless mode. The stock estimator can temporarily report a high value
during charging transitions; this release does not claim to fix that estimate.

Permanent firmware does not use a hardware watchdog. A hung application can
require a physical power cycle; that is not automatic rollback. Battery
endurance and runtime stack watermarks remain unqualified. Builds verify flash,
RAM reservations and resident identity, and never flash the keyboard.

The source archive includes the board sources, exact ZMK/Zephyr/module inputs
and QMK resident sources. On Linux x86_64, run `make setup setup-zmk`, then
`make build-updater-debug build-zmk-release`; setup verifies the exported source
hashes and installs pinned tools. Normal builds do not download dependencies.

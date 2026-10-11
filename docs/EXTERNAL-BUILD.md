# Building from external GMK70 sources

Builds read the pinned QMK or Vial checkout in `dependencies/` and the maintained
GMK70 files in `firmware/` directly. No framework or board source tree is copied
or symlinked. Dependency setup and standalone source archives are unchanged.

`tools/build-support/external.mk` supplies the external keyboard, keymap,
compatibility and wireless paths to upstream Make. `qmk_generate.py` adapts
keyboard path discovery for upstream's JSON generators within that Python
process; it does not edit upstream files or replace the generators.

Generated headers, objects and binaries live in
`.work/<platform>/<keymap>/<normal|updater>/.build/`. Normal and updater builds
have separate caches. Build preparation verifies dependency pins and source
collisions, records source origins, and preserves existing intermediate files.
`make clean` removes these caches and preserves sources and published artifacts.

VIA still uses the original manually loaded definition. Vial still generates
and embeds its maintained `vial.json`; automatic GUI discovery is unchanged.

Use the existing `make build-*` targets; setup and artifact locations are
unchanged. Standalone `qmk compile` does not discover GMK70 in the pristine
framework checkout. The project build scripts supply the external-source
adapter automatically.

Repository tests exercise upstream generators with both pins, retain Vial's
keymap feature rules, and check source collisions, cache isolation and safe
cleanup. Firmware validation covers all seven normal/updater targets, embedded
Vial definitions, BIN/HEX agreement and package checksums. Repeated QMK builds
reuse objects; upstream Vial's per-build EEPROM magic remains unchanged.

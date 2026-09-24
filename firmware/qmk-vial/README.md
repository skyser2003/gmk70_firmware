# Vial port workspace

Reserved for the future Vial port; no Vial firmware is built yet. `make build-vial` intentionally exits with an explanatory error.

This directory holds Vial-specific additions. Official-QMK/VIA additions live in `firmware/qmk-via/`.

Vial requires code from a specific `vial-qmk` revision and a Vial-specific keymap, UID, unlock combination, and layout definition. Keep these additions and their recorded provenance here, in this repository. The [multi-platform layout](../../docs/FIRMWARE-ARCHITECTURE.md) uses an independent `dependencies/vial-qmk/` submodule and shared board/radio sources under `firmware/qmk-common/`. Avoid maintaining a second board copy. Compatibility adapters specific to the Vial fork belong here.

QMK and Vial have independent dependency pins. No Vial firmware tags were advertised when checked on 2026-09-24; select a firmware release tag if one becomes available, otherwise record an exact tested commit when implementing this port. Do not substitute the GUI application's release version or a moving branch.

Do not reuse another author's Vial UID. Choose and record a project UID when the actual port is implemented. The original VIA definition remains a reference, not a replacement for the Vial definition.

Porting reference: https://get.vial.today/docs/porting-to-vial.html

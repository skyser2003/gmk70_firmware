# QMK upstream comparison

The current migration plan is [Firmware dependency and source layout](FIRMWARE-ARCHITECTURE.md).
It targets the latest verified QMK release tag `0.34.4`, with separate future
Vial and ZMK dependencies. The comparison below remains evidence about the old
bundled revision; its recommendation to retain that revision has been superseded.

Compared on 2026-09-24: this repository at `4fdeec22`, with no pending changes under `firmware/qmk/`, against the official [qmk/qmk_firmware commit `3d4da6de29c8635c9cd232ce456d8fec8d31921b`](https://github.com/qmk/qmk_firmware/commit/3d4da6de29c8635c9cd232ce456d8fec8d31921b), dated 2024-11-12. Nested libraries were compared against the revisions recorded in [UPSTREAM-SOURCES.tsv](provenance/BUNDLED-SOURCES-2024.tsv).

**No functional changes to existing upstream QMK framework or library code were found.** GMK70 support consists of added board and wireless files. An unmodified official QMK submodule at this revision can therefore supply the framework; a custom QMK fork is not required for the current implementation.

This comparison uses the exact revision currently built, not today's `master`. Updating the framework version is a separate compatibility change.

## Framework differences

| Category | Result |
| --- | --- |
| Retained upstream files | 2,752 exact Git blob matches, including the upstream README under its new name |
| Existing framework source edits | None in the retained core, build definitions, platforms, drivers, tests, tooling or documentation |
| Existing file changed | `.gitignore`, replaced with rules for generated files |
| Added GMK70 board files | 24 under `keyboards/zuoya/gmk70/` |
| Added wireless files | 14 under `drivers/wireless/` |
| Added documentation/metadata | Project `README.md` and `UPSTREAM-SOURCES.tsv` |
| Renamed file | Upstream `readme.md` retained as `UPSTREAM-README.md`, with identical contents |
| Omitted upstream files | 18,917 unrelated keyboard files, 5 userspace files, 25 root GitHub configuration files, 2 editor files, and 2 `.gitmodules` files |
| File-mode changes | None among retained files |

The two omitted QMK `.gitmodules` files are the root registration and `lib/usbhost/USB_Host_Shield_2.0/.gitmodules`. The nested ChibiOS-Contrib registration was also removed when its library contents became ordinary files.

The large deletion count reflects removal of unrelated keyboards during bundling, not thousands of GMK70 core patches. A full official submodule would restore that upstream catalog in the dependency checkout.

## Library differences

Every bundled revision matches the corresponding upstream submodule pin, including ChibiOS-Contrib's nested MCUX SDK pin. All retained library contents match apart from carriage-return/line-ending normalization; no library file-mode changes were found.

| Component | Files compared | Files with CR/LF differences | Other differences |
| --- | ---: | ---: | --- |
| ChibiOS | 5,372 | 59 | None |
| ChibiOS-Contrib | 2,541 | 802 | `.gitmodules` omitted |
| MCUX SDK | 6,921 | 1 | None |
| GoogleTest | 259 | 0 | None |
| LUFA | 1,321 | 0 | None |
| LVGL | 1,082 | 3 | None |
| Pico SDK | 557 | 1 | None |
| printf | 16 | 1 | None |
| V-USB | 99 | 0 | None |

The MCUX difference is a carriage return in the tracked NXP license text, not changed license wording. The working copy retains the original bytes. This distinction matters when comparing Git blobs with Windows checkout files.

## What remains specific to GMK70

| Local sources | Purpose |
| --- | --- |
| `keyboards/zuoya/gmk70/keyboard.json`, `config.h`, `halconf.h`, `mcuconf.h` | Matrix, MCU/bootloader selection, RGB, split, memory and peripheral configuration |
| `gmk70.c`, `gmk70.h`, `rgb_matrix_user.inc`, `rgb_record/`, `wls/` | Board behavior, lighting, indicators, settings and radio integration |
| `keymaps/`, `stock_keycodes.c`, `stock_keycodes.h`, `stock_via.c` | Shared default/VIA keymap and original vendor JSON compatibility |
| `split_role.c` | Direct USB master selection for either half |
| `drivers/wireless/` | UART radio protocol, host switching, reports, raw HID and low-power handling |
| `post_rules.mk`, keymap rules and wireless rules | Compile the local additions and connect them to QMK |

The existing official framework and its pinned ChibiOS-Contrib library already supply WB32 support. Local behavior connects through QMK hooks and the host-driver interface. Wireless raw-HID output uses the linker option `--wrap=raw_hid_send`, rather than a patch to QMK's implementation. These integrations explain how the added code works with an unchanged framework.

## Submodule structure supported by the comparison

Keep the project-owned sources and platform separation in this repository, with an official upstream dependency:

```text
dependencies/qmk/                    official qmk/qmk_firmware submodule, pinned revision
firmware/qmk-common/keyboards/zuoya/gmk70/
firmware/qmk-common/drivers/wireless/
firmware/qmk-via/                    official-QMK/VIA keymap and compatibility code
firmware/qmk-vial/                   future Vial-specific work
firmware/zmk/                        future ZMK-specific work
definitions/via/                     original vendor JSON
scripts/                             build orchestration and artifact packaging
tests/                               project regression tests
```

The initial migration composed a disposable build tree from the pinned framework and maintained GMK70 sources. Current builds compile those sources in place using the [external-source adapter](EXTERNAL-BUILD.md), with no source copying or symlinking. External userspace alone still does not register the new board at these pins; the adapter supplies board discovery to upstream JSON generators and external paths to Make. Both dependency checkouts remain unmodified.

A conversion must update project paths, build/clean/test/format scripts, CI checkout, source packaging and the current vendoring policy in `AGENTS.md`. It should initially retain the tested QMK revision, run the existing regression tests, and compare clean VIA/default builds with the current firmware payloads. Later upstream updates should advance the recorded pin deliberately and repeat validation; automatically following every upstream commit would make builds less reproducible.

The compiler/include records identify ChibiOS, ChibiOS-Contrib and printf as the required external libraries for this WB32 target. Selectively initializing those dependencies, without the unused nested MCUX SDK, is a candidate for avoiding the pedometer component identified in [the licensing audit](LICENSING.md); it still needs a clean-build check during conversion. Merely changing a directory into a submodule does not resolve licensing of any files subsequently copied into source packages.

## Verification scope

Git trees, blob contents, file modes and nested revision pins were compared using the retained upstream object databases. The working tree has no firmware changes relative to the compared project commit. The comparison helper passed formatting, lint and strict typing checks; detailed local results are under `artifacts/analysis/qmk-upstream-diff-20260924/`.

This is a comparison and migration assessment. No dependency layout, firmware source, submodule configuration or build output was changed, and no new-version or converted-layout build is claimed.

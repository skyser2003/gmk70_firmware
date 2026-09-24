# Licensing audit

Historical scope: this audit describes the former bundled tree. The current upstream-QMK setup initializes only required libraries and omits the unused MCUX SDK and its pedometer binary from new source exports. See [current provenance](BUNDLED-SOURCES.md); older Git history and archives are unchanged. The board-source and machine-code-array findings still apply.

Reviewed on 2026-09-24 against repository commit `4fdeec22` and the pinned imports in [UPSTREAM-SOURCES.tsv](provenance/BUNDLED-SOURCES-2024.tsv).

The project's GPLv3 license follows the original GMK70 port. Original notices are retained. This is not an unrestricted open-source clearance for the entire bundled tree: an unused proprietary NXP/KEYnetik pedometer library is present, and the preferred source for two inherited WB32 machine-code arrays remains unverified.

## Project and firmware license

The root [LICENSE](../LICENSE) is unchanged from `uk-taniyama/qmk_zuoya_gmk70` commit `8d8055bfc864b36d41bbfb0bf9cd393e4c4b6e20`. Both have Git blob ID `f288702d2fa16d3cdf0035b15a9fcbc552cd88e7` after Git's line-ending normalization.

The GMK70 board and radio sources retain their GPL-2.0-or-later notices. This permits use under GPLv3; it is not the same as GPL-2.0-only. The bundled QMK root GPLv2 text also remains intact. QMK's [official licensing guidance](https://docs.qmk.fm/hardware_keyboard_guidelines#license) requires GPLv3 for ARM firmware using ChibiOS, which applies to the WB32 GMK70 build. See also the [FSF explanation of version compatibility](https://www.gnu.org/licenses/gpl-faq.en.html#v2v3Compatibility).

Copyright holders retain their original rights. Moving sources or removing unrelated product names does not authorize removing copyright notices. The original keymap attribution was moved with the shared implementation to [stock_keymap.h](../firmware/qmk-common/keyboards/zuoya/gmk70/keymaps/stock_keymap.h); the small default/VIA wrappers include it.

## Bundled component notices

Paths below are relative to `firmware/qmk/`. This table identifies principal notices, not a replacement license for every file. Per-file terms and additional notices remain applicable.

| Component | Principal terms and notice location |
| --- | --- |
| QMK | GPL; `LICENSE`, `license_GPLv2.md`, `license_GPLv3.md`, and individual source headers |
| GMK70 and radio | GPL-2.0-or-later headers in `keyboards/zuoya/gmk70/` and `drivers/wireless/` |
| ChibiOS | GPLv3 in `lib/chibios/license.txt`; additional component notices retained |
| ChibiOS-Contrib / WB32 port | WB32 driver headers carry Westberry's Apache-2.0 notice; other contributed components retain their own terms |
| MCUX SDK | Mostly BSD-3-Clause and Apache-2.0; exceptions listed in `lib/chibios-contrib/ext/mcux-sdk/SW-Content-Register.txt`, including the finding below |
| GoogleTest | BSD-3-Clause, `lib/googletest/LICENSE`; additional generator notice retained |
| LUFA | Permissive notice with attribution and author-name restrictions, `lib/lufa/LUFA/License.txt` |
| LVGL | MIT, `lib/lvgl/LICENCE.txt` |
| Pico SDK | BSD-3-Clause, `lib/pico-sdk/LICENSE.TXT`; component notices also retained |
| printf | MIT, `lib/printf/LICENSE` |
| V-USB | GPLv2 or GPLv3 at the recipient's choice, `lib/vusb/usbdrv/License.txt`; optional commercial-license text also retained |
| lib8tion | MIT, `lib/lib8tion/LICENSE` |

Copying these components into one Git repository does not change their licenses to GPLv3.

## Unused proprietary library in the SDK snapshot

The tracked file `firmware/qmk/lib/chibios-contrib/ext/mcux-sdk/middleware/issdk/algorithms/pedometer/lib/libpedometerm4.a` is a 770,084-byte binary library. Its SHA-256 is `9f3b26c310f14095c45d87615d72eb78f615c60dac33b3d2f6ed8f58be58a37c`.

The pinned upstream [content register](https://github.com/NXPmicro/mcux-sdk/blob/102c27d7cc2aaac87c133702e7829a105e0cb433/SW-Content-Register.txt) identifies the pedometer library as KEYnetik code under `LA_OPT_NXP_Software_License`. The [license](https://github.com/NXPmicro/mcux-sdk/blob/102c27d7cc2aaac87c133702e7829a105e0cb433/LA_OPT_NXP_Software_License.txt), sections 2.2-2.3, restricts use/distribution to NXP-based authorized systems and does not provide a general standalone redistribution grant. A public upstream copy does not establish such permission for this repository.

The adjacent BSD-licensed `pedometer.c` is an interface wrapper, not evidence that the compiled algorithm has a BSD license. Its KEYnetik interface header also carries a separate copyright notice.

Neither GMK70 link map references MCUX or this pedometer library. Both current `artifacts/qmk/{via,default}/source.tar.gz` packages nevertheless contain it, as recorded by their source manifests. For an open-source-only distribution, remove the unused pedometer component (or prune the unused SDK with appropriate build validation), update the import documentation, and regenerate source packages. The upstream license/register documents should remain as provenance. Existing Git history and previously distributed archives need separate consideration; changing the working tree does not change those copies.

The register also lists proprietary DSP components, but the referenced `middleware/dsp/nn` and `middleware/dsp/naturedsp/hifi4` directories are absent from this snapshot. The presence of their license entries alone does not establish that those components are bundled.

## Remaining scope limits

- [lpwr_wb32.c](../firmware/qmk-common/drivers/wireless/lpwr_wb32.c) retains its GPL-2.0-or-later header and two inherited executable integer arrays. No separate preferred assembler/C source for those arrays was established in this audit. That is a source-provenance uncertainty, not proof that the file is proprietary.
- The original [VIA JSON](../definitions/via/ZUOYA_GMK70.json) remains unchanged. No separate explicit vendor license for that file was verified; the repository's GPL text alone does not establish the vendor's grant.
- The external radio module, USB receiver, and MCU ROM bootloader retain their existing firmware. Their implementation sources are not supplied by this project.
- The compiler's standard runtime libraries are linked from the pinned build image. This audit checked bundled imports and GMK70 notices; it did not audit every toolchain package or every file's licensing history.

## Checks performed

- Compared 37 license/notice files against the ten original Git-export archives used for bundling. All were retained with matching contents after CRLF/LF normalization.
- Verified those 37 files plus the root license in both existing firmware source archives.
- Checked 43 original copyright/SPDX lines across corresponding GMK70 and radio files, accounting for the shared keymap move. All were retained.
- Inspected both GMK70 link maps and source manifests to distinguish linked firmware code from unused bundled SDK content.

When distributing firmware, provide its matching buildable source and preserve the applicable notices. The current build scripts already generate matching source archives, but their inclusion of the unused proprietary library remains an unresolved distribution issue. This audit changes documentation only; it does not relicense, remove, rebuild, or flash firmware.

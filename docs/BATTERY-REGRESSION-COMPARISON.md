# Battery comparison against the build with broken right-half recovery

Analysis date: 2026-09-27. This is an offline comparison; no keyboard commands or flashing were performed.

The user reports that battery indication worked in the earlier project firmware that prevented a directly connected right half from executing its mapped bootloader key. Two retained builds have that role-selection bug. Their battery implementations are identical, so both were compared with the recovery build, the subsequent stock-keycode build, and current source. The exact binary previously flashed remains unidentified.

**The right-half recovery fix did not remove or rewrite battery indication.** The battery handlers, drawing routines and radio battery processing survived unchanged. There is a later keycode-number change, but it does not by itself explain the reported failure of both old and new battery numbers. The comparison narrows the investigation; it does not establish a runtime cause or a firmware fix.

## Exact comparison inputs

Paths below are relative to the repository root. Each historical directory contains a VIA binary and build metadata. The first three include `source-overlay.tar.gz`; the fourth includes the complete `source.tar.gz`.

| Stage | Retained artifact directory | BIN bytes | Battery key | Right half can become USB master |
| --- | --- | ---: | --- | --- |
| Before selector correction | `artifacts/backups/2026-09-22-before-selector-fix/via/` | 70,244 | `0x7E05` | No |
| Before USB-role correction | `artifacts/backups/2026-09-22-before-usb-role-fix/via/` | 70,244 | `0x7E05` | No |
| After USB-role correction | `artifacts/via/` | 70,260 | `0x7E05` | Yes |
| Stock keycode compatibility, older QMK | `artifacts/qmk/via/` | 69,900 | `0x7E1B` | Yes |
| Current maintained source | Commit `c5c24d42d13f424a4c007a325f2579dbd21bca4e` | See current build evidence | `0x7E1B` | Yes |

All four historical builds identify QMK commit `3d4da6de29c8635c9cd232ce456d8fec8d31921b`. Their manifests identify the same compiler container, and the first three have identical `cflags.txt`. The fourth only changes the wireless source include path in those flags; it does not disable a battery or RGB feature. Current source pins official QMK 0.34.4, commit `08c662f286ddfd12a985f57b584b02eca5af0ae6`.

The older manifests explicitly list local modifications. Consequently, the archived source snapshots, rather than just their reference Git commits, define the compared inputs. A total of 153 selected board/radio files were checked against their archived SHA-256 manifests. Paths were normalized for comparison across the repository reorganizations; source contents were not rewritten.

SHA-256 of `zuoya_gmk70_via.bin`, in table order:

```text
before-selector    1ff7e0695844032fb8c2ca17752ec5b5eb3d3d2c7f8d754dc64a38955e3e6d7e
before-usb-role    aa3ec843b8a258f058c71dc4761f73defaa7c5086ca95159dcda7b7ae6908ebe
after-usb-role     f1d84d38636569e85231efe6db5543340d3da6bf652bbe8a908acd91ffecb62c
stock-abi-old-qmk  eb5bdb3b24d6cb4ee057b6e86d2e67d202f9a0c6641fb3e1d6d1b4ee4751ae34
```

## Changes between stages

### Selector correction

Between the two builds with broken right-half recovery, the only board/radio changes were the selector pins and VIA explanatory labels. Bluetooth moved from C14 to C15, and 2.4 GHz moved from C15 to C14. Neither battery handler nor any radio source changed. This correction affects the interpretation of the physical switch, so selected runtime mode remains relevant to battery diagnosis.

### Right-half recovery correction

The recovery build removed `is_keyboard_master()` from `gmk70.c`, added it in `split_role.c`, and added that file to the build. The old function used only B9. The new function first checks USB/charging detection A7 and returns master when that pin is high; otherwise it uses the same B9 logic.

| B9 half-selection input | A7 USB/charging input | Before | After |
| --- | --- | --- | --- |
| High, normal left half | Low | Master | Master |
| High, normal left half | High | Master | Master |
| Low, normal right half | Low | Slave | Slave |
| Low, normal right half | High | Slave | Master |

The rest of `gmk70.c` is source-token-identical, and `rgb_record.c` is byte-identical between these two snapshots. No battery guard, charging condition, LED mapping, wireless protocol, RGB setting or default keymap changed in this step. The compiled master functions also show the B9-only versus A7-then-B9 checks.

That does not prove that changed roles could never affect a particular powered split setup. It does show that the normal left-half role and its battery implementation were preserved. Reverting this correction would reintroduce the known right-half recovery problem without restoring a missing battery routine.

### Original-JSON compatibility

The later stock-compatible build made custom keycode values explicit to match the unchanged factory JSON:

| Function | Earlier project build | Stock-compatible build and current source |
| --- | --- | --- |
| Held battery percentage, `HS_BATQ` | `0x7E05` | `0x7E1B`, Custom(27) |
| Held charging indication, `KC_BATQ` | `0x7E07` | `0x7E07`, Custom(7) |
| Select 2.4 GHz | Different legacy slot | `0x7E05` |

Device selection/pairing handling was reorganized, persistent Bluetooth-slot validation expanded from slots 1–3 to 1–5, and the stock VIA lighting adapter was added, including effect 45 / Close All. These are meaningful surrounding changes even though the battery routines stayed the same. The default map's symbolic battery name now resolves to the new number; pre-existing EEPROM mappings do not automatically acquire that meaning.

This explains why `0x7E05` could previously display battery while the original JSON labelled it as a device key. It is not a complete diagnosis: the user already tested both `0x7E05` and `0x7E1B` in wireless mode without a bar, and Custom(7) retained its number throughout.

### Official QMK migration and current diagnostics

The later migration changed framework versions, GPIO/UART and host-protocol interfaces, EEPROM layout, and RGB keycode translation. Those changes deserve separate consideration when testing current firmware, but the last confirmed installed firmware reports VIA protocol 12. The current QMK 0.34.4 build reports protocol 13. The current framework migration therefore cannot explain an earlier failure on that confirmed older installed build.

Uncommitted diagnostic instrumentation was reviewed separately from the committed-source comparison. It adds counters and getter responses behind its diagnostic build flag. The existing normal VIA build is byte-identical to the retained current-source baseline: both `.work/qmk/via/zuoya_gmk70_via.bin` and `artifacts/analysis/current-status-20260924/builds/via/zuoya_gmk70_via.bin` have SHA-256 `99542a43c9851a90638bab557ceeef5a7b317eb689e95d6b87f3d9f314c88c7a`.

## Battery paths that did not change

The following are identical across all five source snapshots after ignoring comments and whitespace:

- `HS_BATQ` handler body: request battery display while pressed and `confinfo.devs != DEVS_USB`.
- `KC_BATQ` handler body: set/clear the held charging-indication request.
- `query()` in `rgb_record.c`: draw the percentage bar.
- `bat_indicators()`: charging indication.
- `rgb_matrix_indicators_advanced_kb()`, including its call to `query()`.
- `wireless_post_task()`, `rgb_matrix_wls_indicator()` and the split slave dispatcher.
- Radio `md_getp_bat()`, `md_main_task()`, `md_receive_msg_task()` and `md_send_devctrl()`.

The complete radio `module.c`, `module.h`, `wireless.c` and `lowpower.c` are also source-token-identical across those stages. Housekeeping only changes GPIO API spelling in the current framework port. The changed device-mode callback expands Bluetooth-slot validation; it does not replace the battery-percentage update.

The first and recovery ELFs retain the same compiled sizes for the battery bar (132 bytes), charging indication (180 bytes), RGB callback (268 bytes), wireless post-task (104 bytes), and battery getter (8 bytes). Addresses move as code is added. Function sizes alone do not establish equivalence; the source comparisons supply that evidence. ARM `objcopy` extraction from each of the first three ELFs reproduced its archived BIN exactly.

## Revised interpretation of the missing split messages

The factory firmware sends A4/A5 to propagate battery requests and F0/F1 for charging requests between halves. Those messages are absent from the project snapshots, including both early builds that match the user's reported working-battery period.

This remains a factory-compatibility gap, but it predates the reported regression. It cannot explain why a previously working local left-half indication newly stopped after these changes. Adding those messages alone is therefore not an evidence-based fix for the reported failure. See the separate [factory V13/V15 comparison](OFFICIAL-V13-V15-COMPARISON.md).

## What this establishes and what remains open

The previous working implementation is already retained in current normal source. There is no newly missing battery algorithm to copy back, and the radio battery decoder was not replaced. The strongest source-level compatibility change is the battery keycode number; surrounding selector, device dispatch, lighting and persisted settings can still affect whether an unchanged drawing routine is reached and visible.

The unresolved boundary is between receiving the intended custom key, setting its request in the correct runtime mode, reaching the indicator callback, and producing LED output. Existing diagnostics observe those boundaries, as described in [BATTERY-DIAGNOSTICS.md](BATTERY-DIAGNOSTICS.md). The exact installed binary remains an uncertainty. No further hardware test or firmware change was made for this comparison.

## Evidence and validation

Local evidence is retained under `artifacts/analysis/battery-regression-comparison-20260927/`: four staged source diffs, source inventory, per-function comparison, three ELF disassemblies, and the typed comparison helper with dedicated tests. The helper selects and verifies archive members without extracting an archive over the working tree. Its function matcher is a comparison aid for these inspected files, not a general C parser.

The analysis helper passes formatting, Ruff, strict mypy and eight tests, including assertions against the actual historical snapshots. `make test` also passed: seven native C test executables, seven definition/migration tests and ten repository tests. Its output is retained in `repository-tests.log`; `git diff --check` passed. No maintained firmware code was edited or rebuilt for this source-only investigation.

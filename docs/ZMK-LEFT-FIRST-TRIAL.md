# First actual ZMK trial on the GMK70 left

On 2026-10-27, the actual pinned ZMK application was flashed into the left
staging slot and executed. Windows enumerated **GMK70 Left ZMK Trial** as a
keyboard and consumer-control HID device. The independent watchdog then returned
the left to Vial automatically. No cable move, pad bridge or right-only recovery
was needed during this qualification sequence.

This establishes a bounded left USB ZMK trial. The keyboard is currently back in
Vial, with the trial image retained in staging. Permanent ZMK operation, physical
key input, right-side ZMK, wired split, radio, settings, RGB, encoders and Studio
are not established by this test.

A later [right qualification](ZMK-RIGHT-FIRST-TRIAL.md) executed a separate ZMK
trial through left USB and updated the right application. The identities below
record the first left checkpoint; consult that report for the latest state.

## Live evidence

The exact trial was built from `dependencies/zmk/app`, not the bare assembly
fixtures. Maintained WB32 clock, GPIO and USB integration lives in `firmware/zmk/`.
All upstream dependency checkouts remained unmodified.

| Item | Verified value |
| --- | --- |
| Maintained port commit | `47f5f3f` |
| ZMK | `4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85` |
| Zephyr | `10ba6d0cb38bc3d258775d27982f707599320085` |
| Image | 27,668 bytes at `0x08020000`, CRC32 `2635560326` |
| SHA256 | `5f446dd208e84c27b36c05a3823812a1c542814f9359357f29cdf404932b5045` |
| Initial MSP / reset vector | `0x20001CB0` / `0x0802239D` |
| Linked RAM usage | 8,864 bytes; lower retained record excluded |
| USB | `1D50:615E`, manufacturer `GMK70 experimental`, product `GMK70 Left ZMK Trial` |
| USB observation | 17:52:19.237791 UTC: keyboard usage 1/6 and consumer usage 12/1 |
| Return | RUN, RETURNED, IWDG + PIN reset flags `36`; matching image size/CRC |
| Host recovery check | 30.265 seconds of continuous matching INFO/RESULT, zero retries |

The unique product/manufacturer pair was absent before launch. Another existing
keyboard shared the VID/PID, so VID/PID alone was not accepted as proof. The
[actual trial report](validation/2026-10-27-zmk-left/actual-zmk.json) records the
baseline, appearance, disappearance and verified Vial return.

Three preceding left-only checks passed with the same launcher:

| Check | Result |
| --- | --- |
| [Pre-CRT watchdog probe](validation/2026-10-27-zmk-left/watchdog.json) | IWDG reset, stable Vial return, no retries |
| [Staging return fixture](validation/2026-10-27-zmk-left/staging-return.json) | Software reset, stable Vial return, no retries |
| [Staging hang fixture](validation/2026-10-27-zmk-left/staging-hang.json) | IWDG reset, stable Vial return, no retries |

The full launcher records ENTERED only after watchdog setup returns. These checks
therefore cover both pre-CRT progress and the assembly staging handoff. The
fixtures are separately labeled and are not counted as ZMK execution.

## Keyboard and preservation at the left checkpoint

After the ZMK trial, both applications and resident identities were read through
left USB, and all 263 native configuration getters matched the fresh backup.
The [final verification](validation/2026-10-27-zmk-left/final-verification.json)
contains counts and identities, without private keymap or macro contents. A
[later read-only RESULT](validation/2026-10-27-zmk-left/later-result.json) also
confirmed continued Vial availability and the same completed ZMK trial record.

| Region | Identity at the left checkpoint |
| --- | --- |
| Left application | Experimental unlocked trial updater, 95,772 bytes, CRC32 `44883505` |
| Left application SHA256 | `92b93fb4f46db684950a937cdfae6a0d6081d975b0db5780c6e23193ebaedf11` |
| Right application | Published unlocked Vial 0.1.7, 91,236 bytes, CRC32 `1962194226` |
| Both residents | Unchanged, CRC32 `1770733249`, SHA256 `be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a` |

The launcher uses upstream `VIAL_INSECURE` under the user's explicit development
exception. Ordinary Vial/updater builds retain physical unlock. Trial builds
remain experimental and were not added to the normal Vial release assets.

An earlier runtime probe returned briefly and then lost USB again. The revised
launcher services a possibly inherited watchdog only after a valid left trial
return; new trials receive no feeds. The tested sequence resolved that observed
instability. Exact watchdog persistence across reset remains unmeasured; the
servicing policy does not assume that system reset stops it. See the
[execution contract](ZMK-TRIAL-EXECUTION.md).

## Reproduce the software build

Explicit setup and builds remain separate from device operations:

```sh
make setup-zmk
make build-zmk
make test-zmk
```

The build verifies exact locked, clean dependency checkouts without downloading
sources. Outputs are under `artifacts/zmk/left-trial/`; generated files stay under
`.work/zmk/left-trial/normal/`. The separate trial host runner performs device
operations; no build or test target flashes, resets or writes to a keyboard.

Validation also passed `make test`, `make check-tools`, QMK/VIA release/debug
normal and updater builds, secure and unlocked Vial trial builds, and the actual
ZMK build. Independent reviews covered retained records, full startup/recovery,
USB control callbacks, SoC clock gates and the complete new port.

The next development work can use this left staging path for input validation
and WB32 UART integration while preserving the working right application.
The subsequent [right-side launch qualification](ZMK-RIGHT-FIRST-TRIAL.md) is
recorded separately.

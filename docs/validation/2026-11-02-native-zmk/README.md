# Native ZMK integration validation — 2026-11-02

Both joined halves were updated through LEFT USB to the **same application**:
88,424 bytes, CRC32 **192780560**, SHA256
`f5eabd62735f5ae11e3ce493900a9a992d6cf2bd9af470466f4bce6bd904aa91`.
Each half received exactly one INSTALL request. Both returned automatically;
no cable reconnect, right-only USB connection, pad procedure or resident write
was performed. Both resident CRCs remain **1770733249**.

The installed change uses upstream EC11 and the generic ZMK split cores over the
retained board compatibility transport. It does not enable upstream's UART wire
format or general native GLOBAL behavior fan-out. See the
[implementation scope and memory analysis](../../ZMK-NATIVE-INTEGRATIONS.md).

## Before flashing

The seven affected ARM variants built successfully. `make test`, `make test-zmk`
and `make check-tools` passed, including native tests executing the actual pinned
EC11 driver and both generic split cores. The optional built-resident pin test
was explicitly enabled and passed. An independent review found no unresolved
material issue; the native invalid-Gray-transition policy was documented and
pinned by an additional test.

The canonical Studio ELF reserves **28,322 / 28,544 bytes** of admitted RAM,
leaving **222 bytes** unallocated. The clean-main build baseline used 28,200 bytes:
the integration adds 122 bytes. Total reserved stacks remain **14,336 bytes**;
the four mutable 74-position keymaps remain 4,736 bytes. No stack was reduced.
These are ELF allocations, not measured live stack peaks.

Both installed applications initially reported 83,804 bytes / CRC32 3349640223.
A fresh private Studio backup confirmed four clean saved layers with 74 bindings
each. The RAM comparison above refers to the clean source baseline, not a live
RAM measurement of that older installed image.

## Device evidence

| Successful read-only window | Verified seconds | Transport / relay retries | Reconnects |
| --- | ---: | ---: | ---: |
| Before LEFT install | 70.172 | 0 / 0 | 0 |
| After LEFT, with old RIGHT | 70.188 | 0 / 0 | 0 |
| Before RIGHT install | 70.187 | 0 / 0 | 0 |
| Fresh diagnostic after RIGHT install | 70.203 | 0 / 0 | 0 |
| Final both-new qualification | 70.172 | 0 / 0 | 0 |

These windows verified exact application/resident identities, physical roles,
advancing uptime and zero hardware-watchdog feed calls. LEFT and RIGHT have the
same final image identity. Read-only Studio verification after each install
confirmed **all 296 bindings, four layer names/order, geometry and runtime binding
metadata** unchanged. No layout write or import was needed.

The **first** post-RIGHT strict observer rejected its report with
`Incomplete or retried both-half qualification`. Its report was not saved by
the existing helper, so its precise counter/threshold cannot be reconstructed.
That failed window is not counted as successful evidence, and its cause remains
unresolved. The application was not flashed or reset again: the subsequent
independent diagnostic and final strict qualification both passed as recorded
above. Do not describe the whole session as having zero retries.

Physical typing, turning the knob and visual RGB output were not independently
actuated during this run. Hardware stack watermarks remain unmeasured.

## Source and private evidence

Base commit: `fccfead46bc22627c470fd4b3d81f3cc1900d26f`.
The reviewed working tree was frozen as Git tree
`2d8dd9ad4dd5adabe55eb2db8924840002f4b48a`; its source archive SHA256 is
`48ef94fe87aad60798f8fcf1881d2a4c36ac7394ecfa8841cf631f4d1f3fe6dd` and binary
patch SHA256 is `444fde4246938e2451ff94ac22d375746f21d101c46c796c78a5c22b808c7a4f`.
The case helper's `source_commit` identifies that base, not a clean committed
candidate. The exact tree/archive/patch records identify the actual changes.
Only validation documentation changed after this freeze.

At deployment, no new commit was created because the user's signing key was
unavailable to noninteractive signing; no unsigned commit or push was substituted.
The implementation is subsequently published with the native updater framing and
sanitized validation in a signed commit on `codex/zmk-native-integrations`.

Frozen target cases, INSTALL-once markers, health samples, build/test logs,
source archive and private Studio snapshots remain under
`.work/zmk-native-integrations/`. Personal layout contents are excluded from
this report and version control.

# GMK70 Mac layers, lighting and restart analysis

The user requested four stock-style Windows/Mac layers, native lighting support,
research into OS-dependent startup layers and Studio lighting controls, and an
offline investigation of the failed warm restart. The user is asleep and explicitly
opted out of questions. Implementation choices below are the implementer's calls.

## Constraints

- Maintain this repository, pinned dependencies and resident identity unchanged.
- Do not flash, reset or disconnect the working keyboard during this task. The
  current restart failure requires a physical power cycle, so unattended flashing
  cannot provide a verified, usable result.
- Preserve the existing Studio settings addresses, installed Base/Fn edits and
  behavior IDs. Do not require a factory reset to reveal the Mac layers.
- Keep build/test operations entirely offline with respect to the keyboard.
- No radio, encoder or Vial source changes are in scope.

## Design and alternatives

Use four compiled layers, with Windows Base/Fn at IDs 0/1 and Mac Base/Fn at 2/3.
Restore stock Fn+S / Fn+A mode switches and Mac modifier placement. Layer 0 remains
the startup default; automatic OS detection is research, not a new heuristic or
unreviewed moving module dependency. Standard ZMK layer switches remain editable
in Studio. Existing saved layer orders must retain new compiled IDs at the end.

Keep the two 8 KiB settings banks and page journal. Increase the bounded index to
320 entries but store only record offsets in RAM. Encode canonical
`keymap/l/<0..3>/<0..71>` names compactly in new version-2 pages. Read version-1
pages without rewriting them at mount; compaction converts records atomically.
Old images must reject the newer representation rather than misinterpret it.
Test all 288 maximum-width bindings plus metadata, corruption and power cuts.
Expanding flash banks was rejected because it complicates safe migration.

Use upstream ZMK RGB underglow behaviors and effects with a maintained WB32 LED
strip adapter. Stock hardware is RGB-order WS2812, B15/SPIM2, 36 left and 41 right
LEDs, power pins A5/A8 active high. Present a virtual 77-pixel strip on the central
so effects span both halves. Send right pixels in bounded CRC-protected 16-byte
transaction-11 chunks; complete frames only become visible after all chunks arrive.
Never do LED hardware work in USB callbacks or while holding the wire lock.
Preserve transaction sizes/count and updater packets. Older native right images
ignore the lighting packets safely; right lighting requires the new right image.
A separate custom lighting engine was rejected because native behaviors already
provide Studio-assignable controls. There is no mandate to fork Studio to add a menu.

Tune CDC and RPC rings together to 128 bytes, with real framed request regression
coverage. Budget the Studio RPC stack from compiled stack usage before selecting a
smaller value; preserve the scanner, serial, update-worker and interrupt stacks.
Actual ARM builds and retained-RAM bounds are required; stack-size tuning remains
subject to hardware watermark validation before installation.

Analyze reset phases from recorded evidence, pinned sources, the actual resident
binary and vendor documentation. Report facts, conditional hypotheses and the
next diagnostic discriminator. Do not label a suspected cause as a fix.

## Acceptance

Four correct 72-key layers compile and remain editable; legacy settings still
load; maximum-width settings fit and survive interrupted writes. Native RGB
controls drive an independently tested, bounded hardware adapter and split-frame
transport. Supported affected ARM variants fit and all repository/native/tool
checks pass. Documentation clearly separates offline readiness from installation
and identifies remaining hardware validation.

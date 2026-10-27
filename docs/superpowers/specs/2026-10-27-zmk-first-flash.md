# First actual ZMK flash

The user requested continuous work until ZMK is actually flashed. Preserve the
resident and the working right half. The first ZMK target is a clearly labeled,
staging-linked left USB keyboard trial built from pinned ZMK/Zephyr sources,
not the bare assembly fixtures. Verify actual ZMK execution and USB identity;
do not describe a copied image alone as a running port. Keep builds/tests
separate from explicit device commands. No driver installation.

First diagnose the failed IWDG-dependent path. Runtime fault capture uses the
existing64-byte retained record, not additional RAM. A separate internal record
ABI2 is allowed only for entered/returned empty PROBE; existing ABI1 admission
and staging RUN checks remain unchanged. Its five reserved words hold last MMIO
address, write value, CFSR, fault PC and flags(fault kind low8, write flag bit8).
Record each distinct access before performing it. During an active probe,
captured CPU faults request software reset; ordinary faults retain the upstream
handler behavior. Prove a deliberate fault recovery before retrying IWDG.
TRACE0x15 is read-only, offset0 returns first4words and offset1 returns flags
plus zeros; nonce is echoed. FAULT_TEST0x16 requires native unlock and left role,
uses empty PROBE admission and deliberately executes UDF. Right qualification
and staging RUN remain disabled in the runtime profile.

Pin the existing researched ZMK4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85 and its
Zephyr10ba6d0cb38bc3d258775d27982f707599320085, with exact module revisions.
Initialize only through explicit setup. Sources remain unmodified under
dependencies, maintained WB32 integration under firmware/zmk, generated output
under.work/zmk and artifacts/zmk. Implement Cortex-M3 SoC/clock/GPIO and WB32 USB
device-controller integration against that pinned API, then the actual ZMK
board/keymap. Start awake, polled matrix, USB only; stock radio stays unchanged.

After IWDG recovery is proven, qualify the launcher with bounded return/hang
fixtures on left. Then flash the real staging-linked ZMK trial through left USB,
verify its hardware USB descriptor, and verify return to Vial. Continue resolving
failures instead of stopping at intermediate software milestones. Take fresh
configuration backups and verify application/resident identities around writes.

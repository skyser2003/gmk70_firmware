# Volatile Bluetooth Vial experiment

This is an experimental radio payload, separate from the main MCU's QMK/Vial
firmware. It adds a second HID service with usage FF60/61 and unnumbered 32-byte
reports, forwarding requests through the stock UART32 helper. It preserves the
stock typing service and the existing FFF0 configuration channel. It contains no
flash programming, erase, commit, pairing, or radio reset operation.

The [persistent Web Vial assessment](../../../../docs/WEB-VIAL-PERSISTENCE.md)
records the minimum-radio/MCU-feature split, stock Bluetooth and dongle
compatibility requirements, and the missing recovery evidence. It is an offline
analysis; no persistent radio image is ready or installed.

Only one configuration client may use the keyboard at a time. FFF0 and the new
HID service share the stock UART response stream, which has no transaction IDs.
The adapter preserves every correctly framed response while subscribed, including
late duplicates. It cannot match a delayed response to a particular client retry.

This payload clears its subscription on link create/loss. Automatic restoration
for each bonded host remains incomplete; this is an experimental RAM service.

These bindings support only the captured stock application with SHA256
`100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c` and ROM
`a2214bdee4ec073d5800df66025f65f77c43433385f5ed51cdc2bfcff64f0908`.
Do not infer that another image shares its ABI or addresses.

`stock_abi.h` records only the functional layouts/constants used by this payload.
They were checked against the captured instructions and Freqchip's SDK header
at mirror commit `9a8a2dd0f7e058685bc3b074ee875f8daf2a3fb6`,
`components/ble/include/gatt/gatt_api.h`, SHA256
`e78d554ccc19248f2935f69ce7361a0cc02527d036922536f34f7955c3fc4e87`.
No vendor implementation or library is included or linked here. The Service
Changed message matches that pinned SDK's eight-byte ABI and uses captured stock
kernel veneers. The reconnect helper calls the captured stock GAP disconnect API.

For offline reproduction, supply the address returned by the actual allocator:

```sh
source scripts/project.sh
bash firmware/qmk-common/radio/ram-vial-trial/build-offline.sh 0x1100C840 2048
```

Replace the example with the address and size returned by the actual allocator.
An address is not a default installation location or a lease for a later session.
Compilation does not establish current ownership. Output goes only under
`.work/radio/ram-vial-trial/normal/`; source trees are neither copied nor linked.
No normal QMK/Vial target includes this experiment, and builds perform no device
I/O or downloads. There is deliberately no general installer in this directory.

Physical trial scripts remain separate, local research tools. Before any upload
they must verify the exact stock image/code, current service slots, actual
allocation header and a fresh ownership nonce. They must restrict host writes
to that owned block, the exclusive 178-byte inactive CRC validator, and the
single branch byte verified for this image. The four-byte host callback write
rejected during earlier review remains prohibited. The allocation fragment is
deliberately unarmed until a separately guarded physical operation configures it.

Radio power loss discards the payload. Do not treat simulation as evidence of
current RAM ownership, asynchronous timing, or Windows enumeration. Keep raw
flash/RAM, ownership nonces, HID paths, settings, and browser captures private.

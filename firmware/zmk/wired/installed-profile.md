# Installed Vial wire compatibility

The reference is the full trial build `5308274`, installed before this work,
represented by `artifacts/vial/vial-unlocked-trial-updater/zuoya_gmk70_vial.elf`.
Its SHA-256 is
`7b0661740c2b3fed40bf54eff1ffd25de8f3a372e008db85f078d5d3a48e2bac`.
This evidence does not describe the older minimal trial build.

`split_transaction_table` has size 228 (19 descriptors of 12 bytes). The
little-endian descriptor layout is input size at byte 0, input offset at 2,
output size at 4, output offset at 6, callback at 8. DWARF's
`serial_transaction_id` supplies the following names. Buffer sizes are bytes.

| ID | Name | Input | Output |
|---:|---|---:|---:|
| 0 | GET_SLAVE_MATRIX_CHECKSUM | 0 | 1 |
| 1 | GET_SLAVE_MATRIX_DATA | 0 | 10 |
| 2 | PUT_MASTER_MATRIX | 10 | 0 |
| 3 | GET_ENCODERS_CHECKSUM | 0 | 1 |
| 4 | GET_ENCODERS_DATA | 0 | 8 |
| 5 | CMD_ENCODER_DRAIN | 0 | 0 |
| 6 | PUT_SYNC_TIMER | 4 | 0 |
| 7 | PUT_LAYER_STATE | 1 | 0 |
| 8 | PUT_DEFAULT_LAYER_STATE | 1 | 0 |
| 9 | PUT_LED_STATE | 1 | 0 |
| 10 | PUT_MODS | 4 | 0 |
| 11 | PUT_RGB_MATRIX | 16 | 0 |
| 12 | PUT_ACTIVITY | 12 | 0 |
| 13 | PUT_RPC_INFO | 4 | 0 |
| 14 | PUT_RPC_REQ_DATA | 32 initially; runtime 0..32 | 0 |
| 15 | EXECUTE_RPC | 1 | 0 |
| 16 | GET_RPC_RESP_DATA | 0 | 32 initially; runtime 0..32 |
| 17 | USER_SYNC_MMS | 0 | 0 |
| 18 | GMK_UPDATE_RPC | 0 | 0 |

The responder sends `ID XOR 19` immediately after receiving a valid ID, then
receives the exact input size and sends the exact output size. There is no
extra length byte, terminator, response status, or general frame checksum.
Each matrix data frame is five little-endian uint16 rows. Checksums cover the
10 matrix bytes or the three RPC info payload bytes. RPC info is
`{crc, rpc_id, input_length, output_length}`. Update RPC18 requires 32/32.
USER_SYNC_MMS is consumed as a harmless no-op with its negotiated lengths and
zero response; ZMK does not implement Vial's mouse/media synchronization.
The update payload remains opaque, including its namespace and opcode.

The actual linked `crc8` at 0x08010466 starts at 0xff and uses polynomial 0x31,
without reflection or final XOR. It is the non-table implementation in pinned
`quantum/crc.c`. The optional CRC8_USE_TABLE branch uses a different polynomial
and must not be selected. The native test links the unmodified upstream
implementation and compares every prefix of a 64-byte sequence.

Reproduce the ELF table extraction without a device:

```sh
source scripts/project.sh
firmware/zmk/tools/.venv/bin/python - <<'PY'
from elftools.elf.elffile import ELFFile
import struct
with open('artifacts/vial/vial-unlocked-trial-updater/zuoya_gmk70_vial.elf', 'rb') as f:
    elf = ELFFile(f)
    symbol = elf.get_section_by_name('.symtab').get_symbol_by_name('split_transaction_table')[0]
    section = elf.get_section(symbol['st_shndx'])
    offset = symbol['st_value'] - section['sh_addr']
    data = section.data()[offset:offset + symbol['st_size']]
    assert len(data) == 19 * 12
    for index, (ni, oi, no, oo, callback) in enumerate(struct.iter_unpack('<BxHBxHI', data)):
        print(index, ni, oi, no, oo, hex(callback))
PY
arm-none-eabi-objdump -d artifacts/vial/vial-unlocked-trial-updater/zuoya_gmk70_vial.elf --disassemble=crc8
```

Protocol sources, read without changes:

- `dependencies/vial-qmk/platforms/chibios/drivers/serial_protocol.c`, Copyright
  2022 Stefan Kerkmann, GPL-2.0-or-later.
- `dependencies/vial-qmk/quantum/split_common/{transaction_id_define.h,transactions.c,transport.h}`
  and `quantum/crc.c`, Copyright 2021 QMK, GPL-2.0-or-later.
- `dependencies/vial-qmk/lib/chibios-contrib/os/hal/ports/WB32/LLD/UARTv1/hal_serial_lld.c`,
  Copyright 2021 Westberry Technology (ChangZhou) Corp., Ltd, Apache-2.0.
- `dependencies/vial-qmk/lib/chibios-contrib/os/common/ext/CMSIS/WB32/WB32FQ95xx/wb32fq95xx.h`
  for register locations/flags; exact sources remain pinned by the root lock.

The native UART adapter preserves the vendor's fractional divider, FIFO and
clock choices. Its A9/A10 AF7 pins, 115200 8N1 full duplex configuration,
B9-high-left role and C14/C15-high-wired gate match maintained common sources.
Matrix pin arrays are generated from common `keyboard.json`; they are never
maintained here independently. Scanning follows ROW2COL (select column low,
read pulled-up rows, restore column as pulled-up input). Each key's state must
remain stable for five milliseconds. A remote matrix expires after 100 ms
without a successful CRC-checked poll, generating releases through kscan.
There are no radio, EEPROM, or flash operations in this module.
